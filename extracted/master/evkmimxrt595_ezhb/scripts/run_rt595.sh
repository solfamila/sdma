#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
WORKSPACE_DIR=$(cd "$ROOT_DIR/../../.." && pwd)
LINKSERVER=${LINKSERVER_BIN:-$WORKSPACE_DIR/linkserver/extracted/flatten_LinkServer_25.12.83.pkg/Payload/LinkServer}
PROBE=${RT595_PROBE:-DTAYCQLQ}
DEVICE=${RT595_DEVICE:-MIMXRT595S:EVK-MIMXRT595}
VARIANT=${1:-hello-ram}
MODE=${2:-semihost}

"$ROOT_DIR/scripts/build_rt595.sh" "$VARIANT"

case "$VARIANT" in
  hello-ram)
    ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_ram.elf"
    ;;
  master-ram)
    ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_master_ram.elf"
    ;;
  slave-ram)
    ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_slave_ram.elf"
    ;;
  hello-flash)
    ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb.elf"
    ;;
  master-flash)
    ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_master.elf"
    ;;
  slave-flash)
    ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_slave.elf"
    ;;
  *)
    echo "usage: $0 [hello-ram|master-ram|slave-ram|hello-flash|master-flash|slave-flash] [semihost|serial-dtay|serial-gra]" >&2
    exit 2
    ;;
esac

case "$MODE" in
  semihost)
    exec "$LINKSERVER" run -p "$PROBE" --exit-timeout 5 "$DEVICE" "$ELF"
    ;;
  serial-dtay)
    exec "$LINKSERVER" run -p "$PROBE" --mode serial:/dev/cu.usbmodemDTAYCQLQ2:115200 --exit-timeout 6 "$DEVICE" "$ELF"
    ;;
  serial-gra)
    exec "$LINKSERVER" run -p "$PROBE" --mode serial:/dev/cu.usbmodemGRA1CQLQ2:115200 --exit-timeout 6 "$DEVICE" "$ELF"
    ;;
  *)
    echo "usage: $0 [hello-ram|master-ram|slave-ram|hello-flash|master-flash|slave-flash] [semihost|serial-dtay|serial-gra]" >&2
    exit 2
    ;;
esac