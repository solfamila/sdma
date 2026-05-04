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
TRACE32_WRAPPER=${TRACE32_WRAPPER:-$WORKSPACE_DIR/.local/trace32/t32cmd_nostop}
TRACE32_NODE=${RT595_TRACE32_NODE:-127.0.0.1}
TRACE32_TIMEOUT_SECONDS=${RT595_TRACE32_TIMEOUT_SECONDS:-180}
TRACE32_WAIT_MS=${RT595_TRACE32_WAIT_MS:-180000}
TRACE32_RUN_WAIT_SECONDS=${RT595_TRACE32_RUN_WAIT_SECONDS:-15}
MASTER_ELF=${RT595_MASTER_ELF:-$WORKSPACE_DIR/.bazel-bin/src/master/master_pigweed_trace32_ram.elf}
SLAVE_ELF=${RT595_SLAVE_ELF:-$WORKSPACE_DIR/.bazel-bin/src/slave/slave_flash.elf}
SAMPLE_COUNT=${RT595_TRANSFER_TIMING_SAMPLES:-5}
PAYLOAD_BYTES=${RT595_TRANSFER_PAYLOAD_BYTES:-255}
CORE_CLOCK_HZ=${RT595_MASTER_CORE_CLOCK_HZ:-198000000}
STOP_ON_FAIL=${RT595_TRANSFER_TIMING_STOP_ON_FAIL:-1}
BUILD_ARTIFACTS=${RT595_TRANSFER_TIMING_BUILD:-1}
output_file=${RT595_TRANSFER_TIMING_OUTPUT:-$WORKSPACE_DIR/.local/trace32/master_slave_transfer_timing_results.tsv}

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

trace32_run_generated_script() {
  local script_file
  local status

  if [[ ! -x "$TRACE32_WRAPPER" ]]; then
    printf 'TRACE32 wrapper not found: %s\n' "$TRACE32_WRAPPER" >&2
    return 1
  fi

  script_file=$(mktemp -t rt595_tts_trace32)
  mv "$script_file" "$script_file.cmm"
  script_file="$script_file.cmm"
  cat > "$script_file"
  set +e
  "$TRACE32_WRAPPER" "node=$TRACE32_NODE" "timeout=$TRACE32_TIMEOUT_SECONDS" "wait=$TRACE32_WAIT_MS" "DO $script_file"
  status=$?
  set -e
  rm -f "$script_file"
  return "$status"
}

rt595_summary_file() {
  printf '%s/.local/trace32/master_slave_transfer_timing_summary.txt\n' "$WORKSPACE_DIR"
}

rt595_legacy_matrix_env_guard() {
  local legacy_var

  for legacy_var in \
    RT595_BLOCK_STREAM_MATRIX_BLOCK_BYTES \
    RT595_BLOCK_STREAM_MATRIX_COUNTS \
    RT595_BLOCK_STREAM_MATRIX_SETTLES_US \
    RT595_BLOCK_STREAM_MATRIX_WRITE_READ_DELAY_LOOPS \
    RT595_BLOCK_STREAM_MATRIX_OUTPUT \
    RT595_BLOCK_STREAM_MATRIX_STOP_ON_FAIL; do
    if [[ -n "${!legacy_var:-}" ]]; then
      printf 'Legacy stream-matrix env %s is no longer supported by %s.\n' \
        "$legacy_var" "$0" >&2
      printf 'This runner now measures the committed 255-byte baseline transfer without firmware instrumentation.\n' >&2
      return 1
    fi
  done
}

rt595_build_artifacts() {
  if [[ "$BUILD_ARTIFACTS" == "0" ]]; then
    return 0
  fi

  (cd "$WORKSPACE_DIR" && bazelisk build //src/master:master_pigweed_trace32_ram.elf //src/slave:slave_flash.elf)
}

rt595_normalize_hex() {
  local value=${1:-}
  value=${value#0x}
  printf '%s' "$value" | tr '[:lower:]' '[:upper:]'
}

rt595_hex_to_dec() {
  local value=${1:-}

  if [[ -z "$value" ]]; then
    printf ''
    return 0
  fi

  value=${value#0x}
  printf '%s' "$((16#$value))"
}

rt595_extract_field() {
  local key=$1
  local signature=$2

  awk -v key="$key" '{for (i = 1; i <= NF; i++) if ($i ~ ("^" key "=")) {sub("^" key "=", "", $i); print $i; exit}}' <<< "$signature"
}

rt595_usec_from_cycles() {
  local cycles=${1:-}

  if [[ -z "$cycles" ]]; then
    printf ''
    return 0
  fi

  awk -v cycles="$cycles" -v hz="$CORE_CLOCK_HZ" 'BEGIN { printf "%.3f", (cycles * 1000000.0) / hz }'
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

rt595_write_transfer_start_line() {
  local source_file=$WORKSPACE_DIR/src/master/variants/ezh_test_master_pigweed.c

  awk '
    /masterXfer\.data[[:space:]]*=[[:space:]]*ezh_data_buffer;/ {
      armed = 1
      next
    }
    armed && /run_transfer_blocking\(&masterXfer\);/ {
      print NR
      exit
    }
  ' "$source_file"
}

rt595_roundtrip_success_line() {
  local source_file=$WORKSPACE_DIR/src/master/variants/ezh_test_master_pigweed.c

  awk '/master: roundtrip compare success/ { print NR; exit }' "$source_file"
}

rt595_line_break_pc() {
  local master_elf=$1
  local source_line=$2
  local objdump_file

  objdump_file=$(mktemp -t rt595_objdump)
  arm-none-eabi-objdump -d -C --line-numbers "$master_elf" > "$objdump_file"
  awk -v source_line="$source_line" '
    index($0, "ezh_test_master_pigweed.c:" source_line) {
      want = 1
      next
    }
    want && /^[0-9a-f]+:/ {
      address = $1
      sub(/:$/, "", address)
      print address
      exit
    }
  ' "$objdump_file"
  rm -f "$objdump_file"
}

rt595_run_trace32_timing_sample() {
  local master_elf=$1
  local summary_file=$2
  local start_pc=$3
  local stop_pc=$4

  trace32_run_generated_script <<EOF
LOCAL &outfile &line &startpc
&outfile="$summary_file"

RESet
SYStem.RESet
SYStem.CPU IMXRT595-CM33
IF COMBIPROBE()||UTRACE()
(
  SYStem.CONFIG.CONNECTOR MIPI20T
)
SYStem.Option DUALPORT ON
SYStem.MemAccess DAP
SYStem.JtagClock 10MHz
ETM.OFF
ITM.OFF
SYStem.Up

Data.LOAD.Elf "$master_elf"
Break.Delete
Break.Set 0x$start_pc /Program

Go
WAIT !STATE.RUN() ${TRACE32_RUN_WAIT_SECONDS}.s
IF STATE.RUN()
(
  Break
  WAIT !STATE.RUN() 1.s
)
&startpc=Register(PC)

Break.Delete
Break.Set 0x$stop_pc /Program
Data.Set D:0xE000EDFC %Long 0x01000000
Data.Set D:0xE0001004 %Long 0x00000000
Data.Set D:0xE0001000 %Long 0x00000001

Go
WAIT !STATE.RUN() ${TRACE32_RUN_WAIT_SECONDS}.s
IF STATE.RUN()
(
  Break
  WAIT !STATE.RUN() 1.s
)

OPEN #1 "&outfile" /Create /Write
SPRINTF &line "ttsTiming= start=%x startpc=%x stop=%x pc=%x cycles=%x status=%x sdmaIrqSeen=%x" 0x$start_pc &startpc 0x$stop_pc Register(PC) Data.Long(D:0xE0001004) Var.VALUE(g_completionStatus) Var.VALUE(g_sdmaIrqSeen)
WRITE #1 "&line"
SPRINTF &line "ttsBuffers= tx0=%x tx1=%x tx2=%x tx3=%x rx0=%x rx1=%x rx2=%x rx3=%x tx254=%x rx254=%x" Var.VALUE(ezh_data_buffer[0]) Var.VALUE(ezh_data_buffer[1]) Var.VALUE(ezh_data_buffer[2]) Var.VALUE(ezh_data_buffer[3]) Var.VALUE(ezh_data_buffer_rx[0]) Var.VALUE(ezh_data_buffer_rx[1]) Var.VALUE(ezh_data_buffer_rx[2]) Var.VALUE(ezh_data_buffer_rx[3]) Var.VALUE(ezh_data_buffer[254]) Var.VALUE(ezh_data_buffer_rx[254])
WRITE #1 "&line"
CLOSE #1

ENDDO
EOF
}

rt595_write_line() {
  local line=$1

  printf '%s\n' "$line"
  printf '%s\n' "$line" >> "$output_file"
}

run_sample() {
  local sample_index=$1
  local start_pc=$2
  local stop_pc=$3
  local summary_file
  local run_output=""
  local summary_output=""
  local run_status=0
  local case_status=unknown
  local timing_signature=""
  local buffer_signature=""
  local start_hex=""
  local startpc_hex=""
  local stop_hex=""
  local pc_hex=""
  local cycles_hex=""
  local status_hex=""
  local sdma_hex=""
  local tx0_hex=""
  local tx1_hex=""
  local tx2_hex=""
  local tx3_hex=""
  local rx0_hex=""
  local rx1_hex=""
  local rx2_hex=""
  local rx3_hex=""
  local tx254_hex=""
  local rx254_hex=""
  local cycles=""
  local elapsed_us=""
  local payload_kib_per_s=""
  local bus_kib_per_s=""
  local line

  printf 'Running sample_%s (payload_bytes=%s core_clock_hz=%s start_pc=0x%s stop_pc=0x%s)\n' \
    "$sample_index" "$PAYLOAD_BYTES" "$CORE_CLOCK_HZ" "$start_pc" "$stop_pc" >&2

  rt595_prepare_slave_image "$SLAVE_ELF"
  summary_file=$(rt595_summary_file)
  rm -f "$summary_file"

  set +e
  run_output=$(rt595_run_trace32_timing_sample "$MASTER_ELF" "$summary_file" "$start_pc" "$stop_pc" 2>&1)
  run_status=$?
  set -e

  if [[ $run_status -ne 0 ]]; then
    case_status=runner-fail
    printf '%s\n' "$run_output" | tail -n 20 >&2
  elif [[ ! -f "$summary_file" ]]; then
    case_status=runner-fail
    printf 'TRACE32 did not produce a timing summary at %s.\n' "$summary_file" >&2
  else
    summary_output=$(cat "$summary_file")
  fi

  timing_signature=$(printf '%s\n' "$summary_output" | grep 'ttsTiming=' | tail -n 1 || true)
  buffer_signature=$(printf '%s\n' "$summary_output" | grep 'ttsBuffers=' | tail -n 1 || true)

  if [[ -n "$timing_signature" && -n "$buffer_signature" ]]; then
    start_hex=$(rt595_normalize_hex "$(rt595_extract_field start "$timing_signature")")
    startpc_hex=$(rt595_normalize_hex "$(rt595_extract_field startpc "$timing_signature")")
    stop_hex=$(rt595_normalize_hex "$(rt595_extract_field stop "$timing_signature")")
    pc_hex=$(rt595_normalize_hex "$(rt595_extract_field pc "$timing_signature")")
    cycles_hex=$(rt595_normalize_hex "$(rt595_extract_field cycles "$timing_signature")")
    status_hex=$(rt595_normalize_hex "$(rt595_extract_field status "$timing_signature")")
    sdma_hex=$(rt595_normalize_hex "$(rt595_extract_field sdmaIrqSeen "$timing_signature")")
    tx0_hex=$(rt595_normalize_hex "$(rt595_extract_field tx0 "$buffer_signature")")
    tx1_hex=$(rt595_normalize_hex "$(rt595_extract_field tx1 "$buffer_signature")")
    tx2_hex=$(rt595_normalize_hex "$(rt595_extract_field tx2 "$buffer_signature")")
    tx3_hex=$(rt595_normalize_hex "$(rt595_extract_field tx3 "$buffer_signature")")
    rx0_hex=$(rt595_normalize_hex "$(rt595_extract_field rx0 "$buffer_signature")")
    rx1_hex=$(rt595_normalize_hex "$(rt595_extract_field rx1 "$buffer_signature")")
    rx2_hex=$(rt595_normalize_hex "$(rt595_extract_field rx2 "$buffer_signature")")
    rx3_hex=$(rt595_normalize_hex "$(rt595_extract_field rx3 "$buffer_signature")")
    tx254_hex=$(rt595_normalize_hex "$(rt595_extract_field tx254 "$buffer_signature")")
    rx254_hex=$(rt595_normalize_hex "$(rt595_extract_field rx254 "$buffer_signature")")

    cycles=$(rt595_hex_to_dec "$cycles_hex")
    elapsed_us=$(rt595_usec_from_cycles "$cycles")
    payload_kib_per_s=$(rt595_rate_kib_per_s "$PAYLOAD_BYTES" "$elapsed_us")
    bus_kib_per_s=$(rt595_rate_kib_per_s "$((PAYLOAD_BYTES * 2))" "$elapsed_us")

    if [[ "$startpc_hex" == "$start_hex" && "$pc_hex" == "$stop_hex" && "$status_hex" == "0" && "$sdma_hex" == "1" && \
          "$tx0_hex" == "1" && "$tx1_hex" == "2" && "$tx2_hex" == "3" && "$tx3_hex" == "4" && \
          "$rx0_hex" == "1" && "$rx1_hex" == "2" && "$rx2_hex" == "3" && "$rx3_hex" == "4" && \
          "$tx254_hex" == "FF" && "$rx254_hex" == "FF" ]]; then
      case_status=pass
    else
      case_status=fail
    fi
  fi

  printf -v line '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s' \
    "sample_$sample_index" "$PAYLOAD_BYTES" "$((PAYLOAD_BYTES * 2))" "$CORE_CLOCK_HZ" "$case_status" "$run_status" \
    "$cycles" "$elapsed_us" "$payload_kib_per_s" "$bus_kib_per_s" "$startpc_hex" "$pc_hex" "$start_hex" \
    "$stop_hex" "${timing_signature} ${buffer_signature}"
  rt595_write_line "$line"

  printf '  -> sample_%s: status=%s cycles=%s elapsed_us=%s payload_kib_per_s=%s bus_kib_per_s=%s\n' \
    "$sample_index" "$case_status" "$cycles" "$elapsed_us" "$payload_kib_per_s" "$bus_kib_per_s" >&2

  if [[ "$STOP_ON_FAIL" == "1" && "$case_status" != "pass" ]]; then
    return 1
  fi

  return 0
}

rt595_legacy_matrix_env_guard
rt595_build_artifacts

if [[ ! -f "$MASTER_ELF" ]]; then
  printf 'Master ELF not found: %s\n' "$MASTER_ELF" >&2
  exit 1
fi

if [[ ! -f "$SLAVE_ELF" ]]; then
  printf 'Slave ELF not found: %s\n' "$SLAVE_ELF" >&2
  exit 1
fi

start_line=$(rt595_write_transfer_start_line)
if [[ -z "$start_line" ]]; then
  printf 'Failed to find the write transfer start line in the master source.\n' >&2
  exit 1
fi

stop_line=$(rt595_roundtrip_success_line)
if [[ -z "$stop_line" ]]; then
  printf 'Failed to find the roundtrip success line in the master source.\n' >&2
  exit 1
fi

start_pc=$(rt595_line_break_pc "$MASTER_ELF" "$start_line")
stop_pc=$(rt595_line_break_pc "$MASTER_ELF" "$stop_line")
if [[ -z "$start_pc" || -z "$stop_pc" ]]; then
  printf 'Failed to resolve TRACE32 timing breakpoints from %s\n' "$MASTER_ELF" >&2
  exit 1
fi

mkdir -p "$(dirname "$output_file")"
: > "$output_file"

printf -v header '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s' \
  'sample' 'payload_bytes' 'bus_bytes' 'core_clock_hz' 'case_status' 'run_status' 'cycles' 'elapsed_us' \
  'payload_kib_per_s' 'bus_kib_per_s' 'start_hit_pc' 'stop_hit_pc' 'start_pc' 'stop_pc' 'signature'
rt595_write_line "$header"

for ((sample_index = 1; sample_index <= SAMPLE_COUNT; sample_index++)); do
  run_sample "$sample_index" "$start_pc" "$stop_pc" || {
    printf 'Wrote partial results to %s\n' "$output_file" >&2
    exit 1
  }
done

printf 'Wrote results to %s\n' "$output_file" >&2