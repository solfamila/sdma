#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
WORKSPACE_DIR=$(cd "$ROOT_DIR/../.." && pwd)
BAZEL=${BAZELISK_BIN:-bazelisk}
VARIANT=${1:-master-pigweed-ram}
MODE=${2:-semihost}

case "$VARIANT" in
  master-pigweed-ram)
    TARGET="//:run_master_pigweed_ram"
    DEFAULT_PROBE="PRASAQKQ"
    ;;
  slave-ram)
    TARGET="//:run_slave_ram"
    DEFAULT_PROBE="GRA1CQLQ"
    ;;
  *)
    echo "usage: $0 [master-pigweed-ram|slave-ram] [semihost|serial-dtay|serial-gra|serial:/dev/...:115200]" >&2
    exit 2
    ;;
esac

export RT595_MODE=${RT595_MODE:-$MODE}
export RT595_PROBE=${RT595_PROBE:-$DEFAULT_PROBE}

cd "$WORKSPACE_DIR"
exec "$BAZEL" run "$TARGET"