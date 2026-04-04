#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=${BUILD_WORKSPACE_DIRECTORY:?Run this target with bazelisk run from the project root}

cd "$ROOT_DIR"
./scripts/build_rt595.sh master-pigweed-ram

printf 'Built %s\n' "$ROOT_DIR/build_manual/evkmimxrt595_ezhb_master_pigweed_ram.elf"