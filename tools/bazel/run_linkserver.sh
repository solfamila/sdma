#!/usr/bin/env bash
set -euo pipefail

ELF_PATH=$1
DEFAULT_PROBE=$2
DEFAULT_MODE=$3

if [[ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]]; then
  WORKSPACE_DIR=$BUILD_WORKSPACE_DIRECTORY
else
  SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
  WORKSPACE_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
fi
LINKSERVER=${LINKSERVER_BIN:-$WORKSPACE_DIR/.local/linkserver/extracted/flatten_LinkServer_25.12.83.pkg/Payload/LinkServer}
PROBE=${RT595_PROBE:-$DEFAULT_PROBE}
DEVICE=${RT595_DEVICE:-MIMXRT595S:EVK-MIMXRT595}
MODE=${RT595_MODE:-$DEFAULT_MODE}
EXIT_TIMEOUT=${RT595_EXIT_TIMEOUT:-5}

case "$MODE" in
  semihost)
    exec "$LINKSERVER" run -p "$PROBE" --exit-timeout "$EXIT_TIMEOUT" "$DEVICE" "$ELF_PATH"
    ;;
  serial-dtay)
    exec "$LINKSERVER" run -p "$PROBE" --mode serial:/dev/cu.usbmodemDTAYCQLQ2:115200 --exit-timeout "$EXIT_TIMEOUT" "$DEVICE" "$ELF_PATH"
    ;;
  serial-gra)
    exec "$LINKSERVER" run -p "$PROBE" --mode serial:/dev/cu.usbmodemGRA1CQLQ2:115200 --exit-timeout "$EXIT_TIMEOUT" "$DEVICE" "$ELF_PATH"
    ;;
  serial:*)
    exec "$LINKSERVER" run -p "$PROBE" --mode "$MODE" --exit-timeout "$EXIT_TIMEOUT" "$DEVICE" "$ELF_PATH"
    ;;
  *)
    echo "unsupported RT595_MODE: $MODE" >&2
    exit 2
    ;;
esac