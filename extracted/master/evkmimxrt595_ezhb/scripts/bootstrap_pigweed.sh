#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
WORKSPACE_DIR=$(cd "$ROOT_DIR/../../.." && pwd)
PIGWEED_DIR=${PIGWEED_DIR:-$WORKSPACE_DIR/third_party/pigweed}

if [[ -d "$PIGWEED_DIR/pw_log/public" && -d "$PIGWEED_DIR/pw_preprocessor/public" && -d "$PIGWEED_DIR/pw_polyfill/public" ]]; then
  exit 0
fi

mkdir -p "$(dirname "$PIGWEED_DIR")"

if [[ ! -d "$PIGWEED_DIR/.git" ]]; then
  git clone --depth=1 --filter=blob:none --sparse https://github.com/google/pigweed "$PIGWEED_DIR"
fi

git -C "$PIGWEED_DIR" sparse-checkout set pw_log pw_preprocessor pw_polyfill
