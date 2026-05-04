#!/usr/bin/env bash
set -euo pipefail

MASTER_ELF=$1
SLAVE_ELF=$2
DEFAULT_MASTER_PROBE=$3
DEFAULT_SLAVE_PROBE=$4
DEFAULT_MASTER_RUNNER=${5:-linkserver}

if [[ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]]; then
  WORKSPACE_DIR=$BUILD_WORKSPACE_DIRECTORY
else
  SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
  WORKSPACE_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
fi

rt595_resolve_path() {
  local path=$1
  local resolved_dir

  if [[ "$path" == /* ]]; then
    printf '%s\n' "$path"
    return 0
  fi

  if [[ -e "$path" ]]; then
    resolved_dir=$(cd "$(dirname "$path")" && pwd)
    printf '%s/%s\n' "$resolved_dir" "$(basename "$path")"
    return 0
  fi

  printf '%s/%s\n' "$PWD" "$path"
}

MASTER_ELF=$(rt595_resolve_path "$MASTER_ELF")
SLAVE_ELF=$(rt595_resolve_path "$SLAVE_ELF")

. "$WORKSPACE_DIR/tools/bazel/linkserver_common.sh"
LINKSERVER=${LINKSERVER_BIN:-$WORKSPACE_DIR/.local/linkserver/extracted/flatten_LinkServer_25.12.83.pkg/Payload/LinkServer}
DEVICE=${RT595_DEVICE:-MIMXRT595S:EVK-MIMXRT595}
MASTER_PROBE=${RT595_MASTER_PROBE:-${RT595_PROBE:-$DEFAULT_MASTER_PROBE}}
SLAVE_PROBE=${RT595_SLAVE_PROBE:-$DEFAULT_SLAVE_PROBE}
MASTER_RUNNER=${RT595_MASTER_RUNNER:-$DEFAULT_MASTER_RUNNER}
EXIT_TIMEOUT=${RT595_EXIT_TIMEOUT:-5}
TRACE_GDB_PORT=${RT595_TRACE_GDB_PORT:-3333}
DUMP_ON_FAIL=${COWORK_VERIFY_DUMP_ON_FAIL:-0}
FLASH_STATE_DIR=${RT595_FLASH_STATE_DIR:-$WORKSPACE_DIR/.local/state}
SLAVE_RESET_PULSE_MS=${RT595_SLAVE_RESET_PULSE_MS:-50}
SLAVE_RESET_SETTLE=${RT595_SLAVE_RESET_SETTLE:-1}
SLAVE_DUMP_TIMEOUT_SECONDS=${RT595_SLAVE_DUMP_TIMEOUT_SECONDS:-20}
TRACE32_WRAPPER=${TRACE32_WRAPPER:-$WORKSPACE_DIR/.local/trace32/t32cmd_nostop}
TRACE32_NODE=${RT595_TRACE32_NODE:-127.0.0.1}
TRACE32_TIMEOUT_SECONDS=${RT595_TRACE32_TIMEOUT_SECONDS:-30}
TRACE32_WAIT_MS=${RT595_TRACE32_WAIT_MS:-30000}
TRACE32_RUN_WAIT_SECONDS=${RT595_TRACE32_RUN_WAIT_SECONDS:-15}
TRACE32_OUTPUT=${RT595_TRACE32_OUTPUT:-$WORKSPACE_DIR/.local/trace32/master_pigweed_trace32_probe.txt}

RT595_TRACE32_OUTPUT=

rt595_ensure_flash_state_dir() {
  mkdir -p "$FLASH_STATE_DIR"
}

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
  rt595_run_linkserver_checked \
    "resetting slave board on probe $SLAVE_PROBE" \
    "$LINKSERVER" probe "$SLAVE_PROBE" wiretimedreset "$SLAVE_RESET_PULSE_MS"

  if [[ "$SLAVE_RESET_SETTLE" != "0" ]]; then
    sleep "$SLAVE_RESET_SETTLE"
  fi
}

rt595_validate_transfer_output() {
  local output=$1
  local status=0

  rt595_expect_linkserver_output "$output" "slave count.*0x00000001" "slave discovery count" || status=$?
  rt595_expect_linkserver_output "$output" "slave addr.*0x00000030" "slave dynamic address" || status=$?
  rt595_expect_linkserver_output "$output" "write transfer complete" "write transfer completion" || status=$?
  rt595_expect_linkserver_output "$output" "read transfer complete" "read transfer completion" || status=$?
  rt595_expect_linkserver_output "$output" "roundtrip compare success" "round-trip compare success" || status=$?

  return "$status"
}

rt595_validate_trace32_output() {
  local output=$1
  local status=0

  rt595_expect_linkserver_output "$output" "status=0" "TRACE32 master completion status" || status=$?
  rt595_expect_linkserver_output "$output" "sdmaIrqSeen=1" "TRACE32 SmartDMA IRQ evidence" || status=$?
  rt595_expect_linkserver_output "$output" "tx0=1 tx1=2 tx2=3 tx3=4" "TRACE32 TX pattern prefix" || status=$?
  rt595_expect_linkserver_output "$output" "rx0=1 rx1=2 rx2=3 rx3=4" "TRACE32 RX pattern prefix" || status=$?
  rt595_expect_linkserver_output "$output" "tx254=ff rx254=ff" "TRACE32 round-trip tail byte" || status=$?

  return "$status"
}

rt595_make_temp_cmm() {
  local tmp

  tmp=$(mktemp -t rt595_trace32)
  mv "$tmp" "$tmp.cmm"
  printf '%s.cmm\n' "$tmp"
}

rt595_run_trace32_master_checked() {
  local context=$1
  local output_file=$2
  local script_file
  local status

  if [[ ! -x "$TRACE32_WRAPPER" ]]; then
    printf 'TRACE32 wrapper not found: %s\n' "$TRACE32_WRAPPER" >&2
    return 1
  fi

  script_file=$(rt595_make_temp_cmm)
  rm -f "$output_file"

  cat >"$script_file" <<EOF
LOCAL &outfile &line
&outfile="$output_file"

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

Data.LOAD.Elf "$MASTER_ELF"
Break.Delete

Go
WAIT !STATE.RUN() ${TRACE32_RUN_WAIT_SECONDS}.s
IF STATE.RUN()
(
  Break
  WAIT !STATE.RUN() 1.s
)

OPEN #1 "&outfile" /Create /Write
SPRINTF &line "pc=%x completed=%x status=%x sdmaIrqSeen=%x" Register(PC) Var.VALUE(g_masterCompletionFlag) Var.VALUE(g_completionStatus) Var.VALUE(g_sdmaIrqSeen)
WRITE #1 "&line"
SPRINTF &line "tx0=%x tx1=%x tx2=%x tx3=%x" Var.VALUE(ezh_data_buffer[0]) Var.VALUE(ezh_data_buffer[1]) Var.VALUE(ezh_data_buffer[2]) Var.VALUE(ezh_data_buffer[3])
WRITE #1 "&line"
SPRINTF &line "rx0=%x rx1=%x rx2=%x rx3=%x" Var.VALUE(ezh_data_buffer_rx[0]) Var.VALUE(ezh_data_buffer_rx[1]) Var.VALUE(ezh_data_buffer_rx[2]) Var.VALUE(ezh_data_buffer_rx[3])
WRITE #1 "&line"
SPRINTF &line "tx254=%x rx254=%x" Var.VALUE(ezh_data_buffer[254]) Var.VALUE(ezh_data_buffer_rx[254])
WRITE #1 "&line"
EOF

  cat >>"$script_file" <<'EOF'
CLOSE #1
ENDDO
EOF

  set +e
  "$TRACE32_WRAPPER" "node=$TRACE32_NODE" "timeout=$TRACE32_TIMEOUT_SECONDS" "wait=$TRACE32_WAIT_MS" "DO $script_file"
  status=$?
  set -e

  if [[ -f "$output_file" ]]; then
    RT595_TRACE32_OUTPUT=$(<"$output_file")
  else
    RT595_TRACE32_OUTPUT=
  fi

  rm -f "$script_file"

  if (( status != 0 )); then
    printf 'TRACE32 failed while %s.\n' "$context" >&2
    printf 'Exit code: %d\n' "$status" >&2
    if [[ -n "$RT595_TRACE32_OUTPUT" ]]; then
      printf '%s\n' "$RT595_TRACE32_OUTPUT" >&2
    fi
  fi

  if [[ -z "$RT595_TRACE32_OUTPUT" ]]; then
    printf 'TRACE32 did not produce a probe summary at %s.\n' "$output_file" >&2
    return 1
  fi

  printf '%s\n' "$RT595_TRACE32_OUTPUT"
  return "$status"
}

rt595_dump_slave_retained_trace() {
  local gdb_bin
  local gdb_cmds
  local gdb_log
  local gdbserver_log
  local gdbserver_pid
  local gdb_status

  gdb_bin=${RT595_GDB_BIN:-$(command -v arm-none-eabi-gdb || true)}
  if [[ -z "$gdb_bin" ]]; then
    printf 'Skipping slave retained trace dump: arm-none-eabi-gdb not found.\n' >&2
    return 0
  fi

  gdb_cmds=$(mktemp "${TMPDIR:-/tmp}/rt595_trace_gdb.XXXXXX")
  gdb_log=$(mktemp "${TMPDIR:-/tmp}/rt595_trace_gdb_output.XXXXXX")
  gdbserver_log=$(mktemp "${TMPDIR:-/tmp}/rt595_trace_gdbserver.XXXXXX")

  cat >"$gdb_cmds" <<EOF
set pagination off
set print pretty on
set confirm off
target remote :$TRACE_GDB_PORT
printf "slave retained trace address:\n"
p/x &g_slaveRetainedTrace
printf "slave retained trace dump:\n"
p/x g_slaveRetainedTrace
detach
quit
EOF

  printf 'Dumping slave retained trace via LinkServer on probe %s\n' "$SLAVE_PROBE" >&2

  set +e
  "$LINKSERVER" gdbserver -p "$SLAVE_PROBE" --semihost-port -1 --gdb-port "$TRACE_GDB_PORT" -a "$DEVICE" \
    >"$gdbserver_log" 2>&1 &
  gdbserver_pid=$!

  /usr/bin/perl -e 'alarm shift @ARGV; exec @ARGV' "$SLAVE_DUMP_TIMEOUT_SECONDS" \
    "$gdb_bin" --batch -q "$SLAVE_ELF" -x "$gdb_cmds" >"$gdb_log" 2>&1
  gdb_status=$?

  if [[ -n "${gdbserver_pid:-}" ]]; then
    if kill -0 "$gdbserver_pid" >/dev/null 2>&1; then
      kill "$gdbserver_pid" >/dev/null 2>&1
    fi
    wait "$gdbserver_pid" >/dev/null 2>&1
  fi
  set -e

  cat "$gdb_log"
  if (( gdb_status != 0 )); then
    cat "$gdbserver_log" >&2
    if (( gdb_status == 142 )); then
      printf 'Slave retained trace dump timed out after %d seconds.\n' "$SLAVE_DUMP_TIMEOUT_SECONDS" >&2
    else
      printf 'Slave retained trace dump failed with exit code %d.\n' "$gdb_status" >&2
    fi
  fi

  rm -f "$gdb_cmds" "$gdb_log" "$gdbserver_log"
  return 0
}

RT595_LINKSERVER_DEVICE=$DEVICE
RT595_LINKSERVER_PROBE=$SLAVE_PROBE
SLAVE_ELF_HASH=$(rt595_file_hash "$SLAVE_ELF")
SLAVE_FLASH_STAMP=$(rt595_slave_flash_stamp_file)

if rt595_slave_flash_required "$SLAVE_ELF_HASH" "$SLAVE_FLASH_STAMP"; then
  rt595_run_linkserver_checked \
    "loading slave image $(basename "$SLAVE_ELF")" \
    "$LINKSERVER" flash -p "$SLAVE_PROBE" "$DEVICE" load -e "$SLAVE_ELF"
  rt595_run_linkserver_checked \
    "verifying slave image $(basename "$SLAVE_ELF")" \
    "$LINKSERVER" flash -p "$SLAVE_PROBE" "$DEVICE" verify "$SLAVE_ELF"

  # LinkServer verify leaves the flashed target halted; re-launch it before
  # starting the master so the slave can answer DAA.
  rt595_run_linkserver_checked \
    "restarting slave image $(basename "$SLAVE_ELF") after verify" \
    "$LINKSERVER" flash -p "$SLAVE_PROBE" "$DEVICE" load -e "$SLAVE_ELF"
  rt595_record_slave_flash "$SLAVE_ELF_HASH" "$SLAVE_FLASH_STAMP"
else
  printf 'Skipping slave flash on probe %s; image unchanged.\n' "$SLAVE_PROBE"
  rt595_reset_slave_board
fi

RT595_LINKSERVER_PROBE=$MASTER_PROBE
run_status=0
set +e
if [[ "$MASTER_RUNNER" == "trace32" ]]; then
  rt595_run_trace32_master_checked \
    "running master image $(basename "$MASTER_ELF") via TRACE32" \
    "$TRACE32_OUTPUT"
else
  rt595_run_linkserver_checked \
    "running master image $(basename "$MASTER_ELF")" \
    "$LINKSERVER" run -p "$MASTER_PROBE" --exit-timeout "$EXIT_TIMEOUT" "$DEVICE" "$MASTER_ELF"
fi
run_status=$?
set -e

if [[ "$MASTER_RUNNER" == "trace32" ]]; then
  OUTPUT=$RT595_TRACE32_OUTPUT
else
  OUTPUT=$RT595_LINKSERVER_OUTPUT
fi
validate_status=0
if (( run_status == 0 )); then
  set +e
  if [[ "$MASTER_RUNNER" == "trace32" ]]; then
    rt595_validate_trace32_output "$OUTPUT"
  else
    rt595_validate_transfer_output "$OUTPUT"
  fi
  validate_status=$?
  set -e
fi

if (( run_status != 0 || validate_status != 0 )); then
  if [[ "$DUMP_ON_FAIL" != "0" ]]; then
    rt595_dump_slave_retained_trace
  else
    printf 'Skipping slave retained trace dump; set COWORK_VERIFY_DUMP_ON_FAIL=1 to enable.\n' >&2
  fi
  if (( run_status != 0 )); then
    exit "$run_status"
  fi
  exit "$validate_status"
fi

printf 'Verified master/slave transfer on probes %s and %s\n' "$MASTER_PROBE" "$SLAVE_PROBE"