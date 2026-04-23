#!/usr/bin/env bash
set -euo pipefail

ELF_PATH=$1
DEFAULT_PROBE=$2

if [[ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]]; then
	WORKSPACE_DIR=$BUILD_WORKSPACE_DIRECTORY
else
	SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
	WORKSPACE_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
fi
. "$WORKSPACE_DIR/tools/bazel/linkserver_common.sh"
LINKSERVER=${LINKSERVER_BIN:-$WORKSPACE_DIR/.local/linkserver/extracted/flatten_LinkServer_25.12.83.pkg/Payload/LinkServer}
PROBE=${RT595_PROBE:-$DEFAULT_PROBE}
DEVICE=${RT595_DEVICE:-MIMXRT595S:EVK-MIMXRT595}

RT595_LINKSERVER_PROBE=$PROBE
RT595_LINKSERVER_DEVICE=$DEVICE

rt595_run_linkserver_checked \
	"loading $(basename "$ELF_PATH") into flash" \
	"$LINKSERVER" flash -p "$PROBE" "$DEVICE" load -e "$ELF_PATH"
rt595_run_linkserver_checked \
	"verifying $(basename "$ELF_PATH") in flash" \
	"$LINKSERVER" flash -p "$PROBE" "$DEVICE" verify "$ELF_PATH"

# LinkServer verify halts the target; launch the verified image again so the
# board is left running when this wrapper exits.
rt595_run_linkserver_checked \
	"restarting $(basename "$ELF_PATH") after verify" \
	"$LINKSERVER" flash -p "$PROBE" "$DEVICE" load -e "$ELF_PATH"