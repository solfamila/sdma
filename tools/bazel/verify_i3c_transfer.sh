#!/usr/bin/env bash
set -euo pipefail

MASTER_ELF=$1
SLAVE_ELF=$2
DEFAULT_MASTER_PROBE=$3
DEFAULT_SLAVE_PROBE=$4

if [[ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]]; then
  WORKSPACE_DIR=$BUILD_WORKSPACE_DIRECTORY
else
  SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
  WORKSPACE_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
fi
. "$WORKSPACE_DIR/tools/bazel/linkserver_common.sh"
LINKSERVER=${LINKSERVER_BIN:-$WORKSPACE_DIR/.local/linkserver/extracted/flatten_LinkServer_25.12.83.pkg/Payload/LinkServer}
DEVICE=${RT595_DEVICE:-MIMXRT595S:EVK-MIMXRT595}
MASTER_PROBE=${RT595_MASTER_PROBE:-${RT595_PROBE:-$DEFAULT_MASTER_PROBE}}
SLAVE_PROBE=${RT595_SLAVE_PROBE:-$DEFAULT_SLAVE_PROBE}
EXIT_TIMEOUT=${RT595_EXIT_TIMEOUT:-5}
TRACE_GDB_PORT=${RT595_TRACE_GDB_PORT:-3333}
DUMP_ON_FAIL=${COWORK_VERIFY_DUMP_ON_FAIL:-0}

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

  "$gdb_bin" -q "$SLAVE_ELF" -x "$gdb_cmds" >"$gdb_log" 2>&1
  gdb_status=$?

  if [[ -n "${gdbserver_pid:-}" ]]; then
    wait "$gdbserver_pid" >/dev/null 2>&1
  fi
  set -e

  cat "$gdb_log"
  if (( gdb_status != 0 )); then
    cat "$gdbserver_log" >&2
    printf 'Slave retained trace dump failed with exit code %d.\n' "$gdb_status" >&2
  fi

  rm -f "$gdb_cmds" "$gdb_log" "$gdbserver_log"
  return 0
}

RT595_LINKSERVER_DEVICE=$DEVICE
RT595_LINKSERVER_PROBE=$SLAVE_PROBE
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

RT595_LINKSERVER_PROBE=$MASTER_PROBE
run_status=0
set +e
rt595_run_linkserver_checked \
  "running master image $(basename "$MASTER_ELF")" \
  "$LINKSERVER" run -p "$MASTER_PROBE" --exit-timeout "$EXIT_TIMEOUT" "$DEVICE" "$MASTER_ELF"
run_status=$?
set -e

OUTPUT=$RT595_LINKSERVER_OUTPUT
validate_status=0
if (( run_status == 0 )); then
  set +e
  rt595_validate_transfer_output "$OUTPUT"
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