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
LINKSERVER=${LINKSERVER_BIN:-$WORKSPACE_DIR/linkserver/extracted/flatten_LinkServer_25.12.83.pkg/Payload/LinkServer}
DEVICE=${RT595_DEVICE:-MIMXRT595S:EVK-MIMXRT595}
MASTER_PROBE=${RT595_MASTER_PROBE:-${RT595_PROBE:-$DEFAULT_MASTER_PROBE}}
SLAVE_PROBE=${RT595_SLAVE_PROBE:-$DEFAULT_SLAVE_PROBE}
EXIT_TIMEOUT=${RT595_EXIT_TIMEOUT:-5}

"$LINKSERVER" flash -p "$SLAVE_PROBE" "$DEVICE" load -e "$SLAVE_ELF"
"$LINKSERVER" flash -p "$SLAVE_PROBE" "$DEVICE" verify "$SLAVE_ELF"

# LinkServer verify leaves the flashed target halted; re-launch it before
# starting the master so the slave can answer DAA.
"$LINKSERVER" flash -p "$SLAVE_PROBE" "$DEVICE" load -e "$SLAVE_ELF"

OUTPUT=$(
  "$LINKSERVER" run -p "$MASTER_PROBE" --exit-timeout "$EXIT_TIMEOUT" "$DEVICE" "$MASTER_ELF" 2>&1
)

printf '%s\n' "$OUTPUT"

printf '%s\n' "$OUTPUT" | grep -q "slave count.*0x00000001"
printf '%s\n' "$OUTPUT" | grep -q "slave addr.*0x00000030"
printf '%s\n' "$OUTPUT" | grep -q "0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x07"
printf '%s\n' "$OUTPUT" | grep -q "0x18 0x19 0x1A 0x1B 0x1C 0x1D 0x1E 0x1F"

printf 'Verified master/slave transfer on probes %s and %s\n' "$MASTER_PROBE" "$SLAVE_PROBE"