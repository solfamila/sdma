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
LINKSERVER=${LINKSERVER_BIN:-$WORKSPACE_DIR/.local/linkserver/extracted/flatten_LinkServer_25.12.83.pkg/Payload/LinkServer}
PROBE=${RT595_PROBE:-$DEFAULT_PROBE}
DEVICE=${RT595_DEVICE:-MIMXRT595S:EVK-MIMXRT595}

"$LINKSERVER" flash -p "$PROBE" "$DEVICE" load -e "$ELF_PATH"
"$LINKSERVER" flash -p "$PROBE" "$DEVICE" verify "$ELF_PATH"

# LinkServer verify halts the target; launch the verified image again so the
# board is left running when this wrapper exits.
"$LINKSERVER" flash -p "$PROBE" "$DEVICE" load -e "$ELF_PATH"