#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
WORKSPACE_DIR=$(cd "$ROOT_DIR/../../.." && pwd)
LINKSERVER=${LINKSERVER_BIN:-$WORKSPACE_DIR/linkserver/extracted/flatten_LinkServer_25.12.83.pkg/Payload/LinkServer}
PROBE=${RT595_PROBE:-DTAYCQLQ}
DEVICE=${RT595_DEVICE:-MIMXRT595S:EVK-MIMXRT595}
VARIANT=${1:-hello-flash}

"$ROOT_DIR/scripts/build_rt595.sh" "$VARIANT"

case "$VARIANT" in
  hello-flash)
    ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb.elf"
    ;;
  pigweed-flash)
    ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_pigweed.elf"
    ;;
  master-flash)
    ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_master.elf"
    ;;
  master-pigweed-flash)
    ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_master_pigweed.elf"
    ;;
  slave-flash)
    ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_slave.elf"
    ;;
  *)
    echo "flash_rt595.sh supports only hello-flash, pigweed-flash, master-flash, master-pigweed-flash, or slave-flash" >&2
    exit 2
    ;;
esac

"$LINKSERVER" flash -p "$PROBE" "$DEVICE" load -e "$ELF"
"$LINKSERVER" flash -p "$PROBE" "$DEVICE" verify "$ELF"
