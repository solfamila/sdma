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
. "$WORKSPACE_DIR/tools/bazel/linkserver_common.sh"
LINKSERVER=${LINKSERVER_BIN:-$WORKSPACE_DIR/.local/linkserver/extracted/flatten_LinkServer_25.12.83.pkg/Payload/LinkServer}
PROBE=${RT595_PROBE:-$DEFAULT_PROBE}
DEVICE=${RT595_DEVICE:-MIMXRT595S:EVK-MIMXRT595}
MODE=${RT595_MODE:-$DEFAULT_MODE}
EXIT_TIMEOUT=${RT595_EXIT_TIMEOUT:-5}

RT595_LINKSERVER_PROBE=$PROBE
RT595_LINKSERVER_DEVICE=$DEVICE

case "$MODE" in
  semihost)
    rt595_run_linkserver_checked \
      "running $(basename "$ELF_PATH") in semihost mode" \
      "$LINKSERVER" run -p "$PROBE" --exit-timeout "$EXIT_TIMEOUT" "$DEVICE" "$ELF_PATH"
    ;;
  serial-dtay)
    rt595_run_linkserver_checked \
      "running $(basename "$ELF_PATH") on /dev/cu.usbmodemDTAYCQLQ2" \
      "$LINKSERVER" run -p "$PROBE" --mode serial:/dev/cu.usbmodemDTAYCQLQ2:115200 --exit-timeout "$EXIT_TIMEOUT" "$DEVICE" "$ELF_PATH"
    ;;
  serial-gra)
    rt595_run_linkserver_checked \
      "running $(basename "$ELF_PATH") on /dev/cu.usbmodemGRA1CQLQ2" \
      "$LINKSERVER" run -p "$PROBE" --mode serial:/dev/cu.usbmodemGRA1CQLQ2:115200 --exit-timeout "$EXIT_TIMEOUT" "$DEVICE" "$ELF_PATH"
    ;;
  serial:*)
    rt595_run_linkserver_checked \
      "running $(basename "$ELF_PATH") on ${MODE#serial:}" \
      "$LINKSERVER" run -p "$PROBE" --mode "$MODE" --exit-timeout "$EXIT_TIMEOUT" "$DEVICE" "$ELF_PATH"
    ;;
  *)
    echo "unsupported RT595_MODE: $MODE" >&2
    exit 2
    ;;
esac