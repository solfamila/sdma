#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
WORKSPACE_DIR=$(cd "$ROOT_DIR/../.." && pwd)
BAZEL=${BAZELISK_BIN:-bazelisk}
VARIANT=${1:-master-pigweed-flash}

case "$VARIANT" in
  master-pigweed-flash)
    TARGET="//:flash_master_pigweed_flash"
    DEFAULT_PROBE="PRASAQKQ"
    ;;
  slave-flash)
    TARGET="//:flash_slave_flash"
    DEFAULT_PROBE="GRA1CQLQ"
    ;;
  *)
    echo "usage: $0 [master-pigweed-flash|slave-flash]" >&2
    exit 2
    ;;
esac

export RT595_PROBE=${RT595_PROBE:-$DEFAULT_PROBE}

cd "$WORKSPACE_DIR"
exec "$BAZEL" run "$TARGET"
