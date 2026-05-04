#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
WORKSPACE_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)

. "$WORKSPACE_DIR/tools/bazel/linkserver_common.sh"

LINKSERVER=${LINKSERVER_BIN:-$WORKSPACE_DIR/.local/linkserver/extracted/flatten_LinkServer_25.12.83.pkg/Payload/LinkServer}
DEVICE=${RT595_DEVICE:-MIMXRT595S:EVK-MIMXRT595}
MASTER_PROBE=${RT595_MASTER_PROBE:-${RT595_PROBE:-PRASAQKQ}}
SLAVE_PROBE=${RT595_SLAVE_PROBE:-GRA1CQLQ}
FLASH_STATE_DIR=${RT595_FLASH_STATE_DIR:-$WORKSPACE_DIR/.local/state}
SLAVE_RESET_PULSE_MS=${RT595_SLAVE_RESET_PULSE_MS:-50}
SLAVE_RESET_SETTLE=${RT595_SLAVE_RESET_SETTLE:-1}
MASTER_ELF=${RT595_MASTER_ELF:-$WORKSPACE_DIR/.bazel-bin/src/master/master_pigweed_measure_quiet_ram.elf}
SLAVE_ELF=${RT595_SLAVE_ELF:-$WORKSPACE_DIR/.bazel-bin/src/slave/slave_flash.elf}
SAMPLE_COUNT=${RT595_TRANSFER_TIMING_SAMPLES:-5}
PAYLOAD_BYTES=${RT595_TRANSFER_PAYLOAD_BYTES:-255}
CORE_CLOCK_HZ=${RT595_MASTER_CORE_CLOCK_HZ:-198000000}
RUN_EXIT_TIMEOUT=${RT595_TRANSFER_TIMING_RUN_EXIT_TIMEOUT:-2}
GDB_PORT=${RT595_TRANSFER_TIMING_GDB_PORT:-3333}
STOP_ON_FAIL=${RT595_TRANSFER_TIMING_STOP_ON_FAIL:-1}
BUILD_ARTIFACTS=${RT595_TRANSFER_TIMING_BUILD:-1}
output_file=${RT595_TRANSFER_TIMING_OUTPUT:-$WORKSPACE_DIR/.local/linkserver/master_slave_transfer_timing_results.tsv}

EXPECTED_MAGIC=1297372243
EXPECTED_VERSION=1
NO_MISMATCH_INDEX=4294967295

rt595_file_hash() {
  local file=$1

  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
  else
    cksum "$file" | awk '{print $1 ":" $2}'
  fi
}

rt595_ensure_flash_state_dir() {
  mkdir -p "$FLASH_STATE_DIR"
}

rt595_slave_flash_stamp_file() {
  printf '%s/slave-flash-%s.sha256\n' "$FLASH_STATE_DIR" "$SLAVE_PROBE"
}

rt595_slave_flash_required() {
  local current_hash=$1
  local stamp_file=$2
  local previous_hash

  if [[ "${RT595_FORCE_SLAVE_FLASH:-0}" == 1 ]]; then
    return 0
  fi

  if [[ ! -f "$stamp_file" ]]; then
    return 0
  fi

  previous_hash=$(tr -d '[:space:]' < "$stamp_file")
  [[ "$previous_hash" != "$current_hash" ]]
}

rt595_record_slave_flash() {
  local current_hash=$1
  local stamp_file=$2

  rt595_ensure_flash_state_dir
  printf '%s\n' "$current_hash" > "$stamp_file"
}

rt595_reset_slave_board() {
  RT595_LINKSERVER_DEVICE=$DEVICE
  RT595_LINKSERVER_PROBE=$SLAVE_PROBE
  rt595_run_linkserver_checked \
    "resetting slave board on probe $SLAVE_PROBE" \
    "$LINKSERVER" probe "$SLAVE_PROBE" wiretimedreset "$SLAVE_RESET_PULSE_MS"

  if [[ "$SLAVE_RESET_SETTLE" != "0" ]]; then
    sleep "$SLAVE_RESET_SETTLE"
  fi
}

rt595_prepare_slave_image() {
  local slave_elf=$1
  local slave_hash
  local stamp_file

  RT595_LINKSERVER_DEVICE=$DEVICE
  RT595_LINKSERVER_PROBE=$SLAVE_PROBE
  slave_hash=$(rt595_file_hash "$slave_elf")
  stamp_file=$(rt595_slave_flash_stamp_file)

  if rt595_slave_flash_required "$slave_hash" "$stamp_file"; then
    rt595_run_linkserver_checked \
      "loading slave image $(basename "$slave_elf")" \
      "$LINKSERVER" flash -p "$SLAVE_PROBE" "$DEVICE" load -e "$slave_elf"
    rt595_run_linkserver_checked \
      "verifying slave image $(basename "$slave_elf")" \
      "$LINKSERVER" flash -p "$SLAVE_PROBE" "$DEVICE" verify "$slave_elf"
    rt595_run_linkserver_checked \
      "restarting slave image $(basename "$slave_elf") after verify" \
      "$LINKSERVER" flash -p "$SLAVE_PROBE" "$DEVICE" load -e "$slave_elf"
    rt595_record_slave_flash "$slave_hash" "$stamp_file"
  else
    printf 'Skipping slave flash on probe %s; image unchanged.\n' "$SLAVE_PROBE"
    rt595_reset_slave_board
  fi
}

rt595_build_artifacts() {
  if [[ "$BUILD_ARTIFACTS" == "0" ]]; then
    return 0
  fi

  (cd "$WORKSPACE_DIR" && bazelisk build //src/master:master_pigweed_measure_quiet_ram.elf //src/slave:slave_flash.elf)
}

rt595_extract_field() {
  local key=$1
  local signature=$2

  awk -v key="$key" '{for (i = 1; i <= NF; i++) if ($i ~ ("^" key "=")) {sub("^" key "=", "", $i); print $i; exit}}' <<< "$signature"
}

rt595_rate_kib_per_s() {
  local bytes=$1
  local usec=$2

  if [[ -z "$usec" ]]; then
    printf ''
    return 0
  fi

  awk -v bytes="$bytes" -v usec="$usec" 'BEGIN { if (usec == 0) { printf ""; exit 0 } printf "%.1f", (bytes * 1000000.0 / usec) / 1024.0 }'
}

rt595_write_line() {
  local line=$1

  printf '%s\n' "$line"
  printf '%s\n' "$line" >> "$output_file"
}

rt595_make_gdb_script() {
  local gdb_script=$1

  cat > "$gdb_script" <<EOF
set pagination off
set confirm off
set print elements 0
target remote :$GDB_PORT
interrupt
printf "ttsTiming= magic=%u version=%u cycles=%u usec=%u status=%u mismatchIndex=%u sdmaIrqSeen=%u\\n", g_measureRetained.magic, g_measureRetained.version, g_measureRetained.cycles, g_measureRetained.usec, g_measureRetained.status, g_measureRetained.mismatchIndex, g_measureRetained.sdmaIrqSeen
printf "ttsBuffers= tx0=%u tx1=%u tx2=%u tx3=%u rx0=%u rx1=%u rx2=%u rx3=%u tx254=%u rx254=%u\\n", g_measureRetained.tx0, g_measureRetained.tx1, g_measureRetained.tx2, g_measureRetained.tx3, g_measureRetained.rx0, g_measureRetained.rx1, g_measureRetained.rx2, g_measureRetained.rx3, g_measureRetained.tx254, g_measureRetained.rx254
detach
quit
EOF
}

rt595_read_measurement() {
  local gdb_script
  local gdbserver_log
  local gdbserver_pid
  local gdb_output=""
  local gdb_status=1
  local attempt

  gdb_script=$(mktemp "${TMPDIR:-/tmp}/rt595_measure_readback.XXXXXX")
  gdbserver_log=$(mktemp "${TMPDIR:-/tmp}/rt595_measure_gdbserver.XXXXXX")
  rt595_make_gdb_script "$gdb_script"

  set +e
  "$LINKSERVER" gdbserver -a -p "$MASTER_PROBE" --semihost-port -1 --gdb-port "$GDB_PORT" "$DEVICE" \
    > "$gdbserver_log" 2>&1 &
  gdbserver_pid=$!

  for attempt in 1 2 3; do
    gdb_output=$(arm-none-eabi-gdb --batch -q "$MASTER_ELF" -x "$gdb_script" 2>&1)
    gdb_status=$?
    if (( gdb_status == 0 )); then
      break
    fi
  done

  if kill -0 "$gdbserver_pid" >/dev/null 2>&1; then
    kill "$gdbserver_pid" >/dev/null 2>&1
  fi
  wait "$gdbserver_pid" >/dev/null 2>&1
  set -e

  if (( gdb_status != 0 )); then
    cat "$gdbserver_log" >&2
    printf '%s\n' "$gdb_output" >&2
  fi

  rm -f "$gdb_script" "$gdbserver_log"
  printf '%s' "$gdb_output"
  return "$gdb_status"
}

run_sample() {
  local sample_index=$1
  local run_output=""
  local readback_output=""
  local run_status=0
  local readback_status=0
  local case_status=unknown
  local timing_signature=""
  local buffer_signature=""
  local magic=""
  local version=""
  local cycles=""
  local usec=""
  local status=""
  local mismatch_index=""
  local sdma_irq_seen=""
  local tx0=""
  local tx1=""
  local tx2=""
  local tx3=""
  local rx0=""
  local rx1=""
  local rx2=""
  local rx3=""
  local tx254=""
  local rx254=""
  local payload_kib_per_s=""
  local bus_kib_per_s=""
  local line

  printf 'Running sample_%s via LinkServer retained readback\n' "$sample_index" >&2

  rt595_prepare_slave_image "$SLAVE_ELF"

  set +e
  run_output=$(rt595_run_linkserver_checked \
    "running quiet measurement image $(basename "$MASTER_ELF")" \
    "$LINKSERVER" run -p "$MASTER_PROBE" --exit-timeout "$RUN_EXIT_TIMEOUT" "$DEVICE" "$MASTER_ELF" 2>&1)
  run_status=$?
  set -e

  if (( run_status != 0 )); then
    case_status=run-fail
    printf '%s\n' "$run_output" | tail -n 20 >&2
  else
    set +e
    readback_output=$(rt595_read_measurement)
    readback_status=$?
    set -e
    if (( readback_status != 0 )); then
      case_status=readback-fail
    fi
  fi

  if [[ "$case_status" == "unknown" ]]; then
    timing_signature=$(printf '%s\n' "$readback_output" | grep 'ttsTiming=' | tail -n 1 || true)
    buffer_signature=$(printf '%s\n' "$readback_output" | grep 'ttsBuffers=' | tail -n 1 || true)

    if [[ -n "$timing_signature" && -n "$buffer_signature" ]]; then
      magic=$(rt595_extract_field magic "$timing_signature")
      version=$(rt595_extract_field version "$timing_signature")
      cycles=$(rt595_extract_field cycles "$timing_signature")
      usec=$(rt595_extract_field usec "$timing_signature")
      status=$(rt595_extract_field status "$timing_signature")
      mismatch_index=$(rt595_extract_field mismatchIndex "$timing_signature")
      sdma_irq_seen=$(rt595_extract_field sdmaIrqSeen "$timing_signature")
      tx0=$(rt595_extract_field tx0 "$buffer_signature")
      tx1=$(rt595_extract_field tx1 "$buffer_signature")
      tx2=$(rt595_extract_field tx2 "$buffer_signature")
      tx3=$(rt595_extract_field tx3 "$buffer_signature")
      rx0=$(rt595_extract_field rx0 "$buffer_signature")
      rx1=$(rt595_extract_field rx1 "$buffer_signature")
      rx2=$(rt595_extract_field rx2 "$buffer_signature")
      rx3=$(rt595_extract_field rx3 "$buffer_signature")
      tx254=$(rt595_extract_field tx254 "$buffer_signature")
      rx254=$(rt595_extract_field rx254 "$buffer_signature")
      payload_kib_per_s=$(rt595_rate_kib_per_s "$PAYLOAD_BYTES" "$usec")
      bus_kib_per_s=$(rt595_rate_kib_per_s "$((PAYLOAD_BYTES * 2))" "$usec")

      if [[ "$magic" == "$EXPECTED_MAGIC" && "$version" == "$EXPECTED_VERSION" && "$status" == "0" && \
            "$mismatch_index" == "$NO_MISMATCH_INDEX" && "$sdma_irq_seen" == "1" && \
            "$tx0" == "1" && "$tx1" == "2" && "$tx2" == "3" && "$tx3" == "4" && \
            "$rx0" == "1" && "$rx1" == "2" && "$rx2" == "3" && "$rx3" == "4" && \
            "$tx254" == "255" && "$rx254" == "255" ]]; then
        case_status=pass
      else
        case_status=fail
      fi
    else
      case_status=readback-empty
    fi
  fi

  printf -v line '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s' \
    "sample_$sample_index" "$case_status" "$cycles" "$usec" "$payload_kib_per_s" "$bus_kib_per_s" \
    "$status" "$mismatch_index" "$sdma_irq_seen" "$run_status" "${timing_signature} ${buffer_signature}"
  rt595_write_line "$line"

  printf '  -> sample_%s: status=%s cycles=%s usec=%s payload_kib_per_s=%s bus_kib_per_s=%s\n' \
    "$sample_index" "$case_status" "$cycles" "$usec" "$payload_kib_per_s" "$bus_kib_per_s" >&2

  if [[ "$STOP_ON_FAIL" == "1" && "$case_status" != "pass" ]]; then
    return 1
  fi

  return 0
}

rt595_build_artifacts

if [[ ! -f "$MASTER_ELF" ]]; then
  printf 'Master ELF not found: %s\n' "$MASTER_ELF" >&2
  exit 1
fi

if [[ ! -f "$SLAVE_ELF" ]]; then
  printf 'Slave ELF not found: %s\n' "$SLAVE_ELF" >&2
  exit 1
fi

mkdir -p "$(dirname "$output_file")"
: > "$output_file"

printf -v header '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s' \
  'sample' 'case_status' 'cycles' 'usec' 'payload_kib_per_s' 'bus_kib_per_s' 'status' 'mismatch_index' 'sdma_irq_seen' 'run_status' 'signature'
rt595_write_line "$header"

for ((sample_index = 1; sample_index <= SAMPLE_COUNT; sample_index++)); do
  run_sample "$sample_index" || {
    printf 'Wrote partial results to %s\n' "$output_file" >&2
    exit 1
  }
done

printf 'Wrote results to %s\n' "$output_file" >&2