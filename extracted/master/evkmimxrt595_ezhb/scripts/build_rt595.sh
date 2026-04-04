#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
WORKSPACE_DIR=$(cd "$ROOT_DIR/../../.." && pwd)
TOOLROOT=${ARM_GNU_TOOLCHAIN:-$WORKSPACE_DIR/toolchains/arm-gnu-toolchain-15.2.rel1-darwin-arm64-arm-none-eabi}
SLAVE_APP_DIR=${RT595_SLAVE_APP_DIR:-$WORKSPACE_DIR/extracted/smartDMA_I3C/slave/slave}
PIGWEED_DIR=${PIGWEED_DIR:-$WORKSPACE_DIR/third_party/pigweed}
COMPILER="$TOOLROOT/bin/arm-none-eabi-gcc"
SIZE_TOOL="$TOOLROOT/bin/arm-none-eabi-size"

VARIANT=${1:-hello-flash}
CONFIG_HEADER="source/mcux_config.h"
VERSION_HEADER="source/mcuxsdk_version.h"
SOURCE_ROOTS=(source startup flash_config device board drivers utilities component)
EXTRA_INCLUDE_DIRS=()
EXTRA_DEFINES=()
USE_PIGWEED=0
VARIANT_SOURCE=""

case "$VARIANT" in
  hello-flash)
    OUTPUT_ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb.elf"
    MAP_FILE="$ROOT_DIR/build_manual/evkmimxrt595_ezhb.map"
    LINKER_SCRIPT="$ROOT_DIR/evkmimxrt595_ezhb_Debug.ld"
    ;;
  hello-ram)
    OUTPUT_ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_ram.elf"
    MAP_FILE="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_ram.map"
    LINKER_SCRIPT="$ROOT_DIR/evkmimxrt595_ezhb_HelloRam.ld"
    ;;
  pigweed-flash)
    OUTPUT_ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_pigweed.elf"
    MAP_FILE="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_pigweed.map"
    LINKER_SCRIPT="$ROOT_DIR/evkmimxrt595_ezhb_Debug.ld"
    USE_PIGWEED=1
    VARIANT_SOURCE="variants/ezh_test_pigweed.c"
    EXTRA_INCLUDE_DIRS=(
      "$ROOT_DIR/pigweed_overrides"
      "$PIGWEED_DIR/pw_log/public"
      "$PIGWEED_DIR/pw_preprocessor/public"
      "$PIGWEED_DIR/pw_polyfill/public"
    )
    EXTRA_DEFINES=(-DPW_LOG_LEVEL_DEFAULT=PW_LOG_LEVEL_INFO)
    ;;
  pigweed-ram)
    OUTPUT_ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_pigweed_ram.elf"
    MAP_FILE="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_pigweed_ram.map"
    LINKER_SCRIPT="$ROOT_DIR/evkmimxrt595_ezhb_HelloRam.ld"
    USE_PIGWEED=1
    VARIANT_SOURCE="variants/ezh_test_pigweed.c"
    EXTRA_INCLUDE_DIRS=(
      "$ROOT_DIR/pigweed_overrides"
      "$PIGWEED_DIR/pw_log/public"
      "$PIGWEED_DIR/pw_preprocessor/public"
      "$PIGWEED_DIR/pw_polyfill/public"
    )
    EXTRA_DEFINES=(-DPW_LOG_LEVEL_DEFAULT=PW_LOG_LEVEL_INFO)
    ;;
  master-ram)
    OUTPUT_ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_master_ram.elf"
    MAP_FILE="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_master_ram.map"
    LINKER_SCRIPT="$ROOT_DIR/evkmimxrt595_ezhb_HelloRam.ld"
    VARIANT_SOURCE="variants/ezh_test_master.c"
    ;;
  master-pigweed-flash)
    OUTPUT_ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_master_pigweed.elf"
    MAP_FILE="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_master_pigweed.map"
    LINKER_SCRIPT="$ROOT_DIR/evkmimxrt595_ezhb_Debug.ld"
    USE_PIGWEED=1
    VARIANT_SOURCE="variants/ezh_test_master_pigweed.c"
    EXTRA_INCLUDE_DIRS=(
      "$ROOT_DIR/pigweed_overrides"
      "$PIGWEED_DIR/pw_log/public"
      "$PIGWEED_DIR/pw_preprocessor/public"
      "$PIGWEED_DIR/pw_polyfill/public"
    )
    EXTRA_DEFINES=(-DPW_LOG_LEVEL_DEFAULT=PW_LOG_LEVEL_INFO)
    ;;
  master-pigweed-ram)
    OUTPUT_ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_master_pigweed_ram.elf"
    MAP_FILE="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_master_pigweed_ram.map"
    LINKER_SCRIPT="$ROOT_DIR/evkmimxrt595_ezhb_HelloRam.ld"
    USE_PIGWEED=1
    VARIANT_SOURCE="variants/ezh_test_master_pigweed.c"
    EXTRA_INCLUDE_DIRS=(
      "$ROOT_DIR/pigweed_overrides"
      "$PIGWEED_DIR/pw_log/public"
      "$PIGWEED_DIR/pw_preprocessor/public"
      "$PIGWEED_DIR/pw_polyfill/public"
    )
    EXTRA_DEFINES=(-DPW_LOG_LEVEL_DEFAULT=PW_LOG_LEVEL_INFO)
    ;;
  slave-flash)
    OUTPUT_ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_slave.elf"
    MAP_FILE="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_slave.map"
    LINKER_SCRIPT="$ROOT_DIR/evkmimxrt595_ezhb_Debug.ld"
    CONFIG_HEADER="$SLAVE_APP_DIR/mcux_config.h"
    VERSION_HEADER="$SLAVE_APP_DIR/mcuxsdk_version.h"
    SOURCE_ROOTS=(startup flash_config device drivers utilities component "$SLAVE_APP_DIR")
    EXTRA_INCLUDE_DIRS=("$SLAVE_APP_DIR")
    EXTRA_DEFINES=(-DAPP_ENABLE_SEMIHOST=0)
    ;;
  slave-ram)
    OUTPUT_ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_slave_ram.elf"
    MAP_FILE="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_slave_ram.map"
    LINKER_SCRIPT="$ROOT_DIR/evkmimxrt595_ezhb_HelloRam.ld"
    CONFIG_HEADER="$SLAVE_APP_DIR/mcux_config.h"
    VERSION_HEADER="$SLAVE_APP_DIR/mcuxsdk_version.h"
    SOURCE_ROOTS=(startup flash_config device drivers utilities component "$SLAVE_APP_DIR")
    EXTRA_INCLUDE_DIRS=("$SLAVE_APP_DIR")
    EXTRA_DEFINES=(-DAPP_ENABLE_SEMIHOST=0)
    ;;
  master-flash)
    OUTPUT_ELF="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_master.elf"
    MAP_FILE="$ROOT_DIR/build_manual/evkmimxrt595_ezhb_master.map"
    LINKER_SCRIPT="$ROOT_DIR/evkmimxrt595_ezhb_Debug.ld"
    VARIANT_SOURCE="variants/ezh_test_master.c"
    ;;
  *)
    echo "usage: $0 [hello-flash|hello-ram|pigweed-flash|pigweed-ram|master-flash|master-ram|master-pigweed-flash|master-pigweed-ram|slave-flash|slave-ram]" >&2
    exit 2
    ;;
esac

if [[ "$USE_PIGWEED" -eq 1 ]]; then
  "$ROOT_DIR/scripts/bootstrap_pigweed.sh"
fi

mkdir -p "$ROOT_DIR/build_manual"

cd "$ROOT_DIR"
SOURCES=()
while IFS= read -r src; do
  SOURCES+=("$src")
done < <(find "${SOURCE_ROOTS[@]}" -type f \( -name '*.c' -o -name '*.S' \) | sort)

if [[ "$USE_PIGWEED" -ne 1 ]]; then
  FILTERED=()
  for src in "${SOURCES[@]}"; do
    if [[ "$src" != "source/pigweed_log_backend.c" && "$src" != "source/syscalls.c" ]]; then
      FILTERED+=("$src")
    fi
  done
  SOURCES=("${FILTERED[@]}")
fi

if [[ -n "$VARIANT_SOURCE" ]]; then
  FILTERED=()
  for src in "${SOURCES[@]}"; do
    if [[ "$src" != "source/ezh_test.c" ]]; then
      FILTERED+=("$src")
    fi
  done
  SOURCES=("${FILTERED[@]}" "$VARIANT_SOURCE")
fi

if [[ "$VARIANT" == "slave-flash" || "$VARIANT" == "slave-ram" ]]; then
  FILTERED=()
  for src in "${SOURCES[@]}"; do
    if [[ "$src" != "$SLAVE_APP_DIR/i3c_interrupt_b2b_transfer_slave_each_4byte.c" ]]; then
      FILTERED+=("$src")
    fi
  done
  SOURCES=("${FILTERED[@]}")
fi

COMMON_FLAGS=(
  -std=gnu99
  -mthumb
  -mcpu=cortex-m33
  -mfpu=fpv5-sp-d16
  -mfloat-abi=hard
  -ffunction-sections
  -fdata-sections
  -fno-builtin
  -fno-common
  -g3
  -Og
  -DCPU_MIMXRT595SFFOC
  -DCPU_MIMXRT595SFFOC_cm33
  -DMCUXPRESSO_SDK
  -DBOOT_HEADER_ENABLE=1
  -DFSL_SDK_DRIVER_QUICK_ACCESS_ENABLE=1
  -DSDK_DEBUGCONSOLE=1
  -DMCUX_META_BUILD
  -DMIMXRT595S_cm33_SERIES
  -DCR_INTEGER_PRINTF
  -DPRINTF_FLOAT_ENABLE=0
  -D__MCUXPRESSO
  -D__USE_CMSIS
  -DDEBUG
  -include "$CONFIG_HEADER"
  -include "$VERSION_HEADER"
  -Isource
  -Iflash_config
  -ICMSIS
  -ICMSIS/m-profile
  -Idevice
  -Idevice/periph
  -Idrivers
  -Idrivers/rt500
  -Iutilities
  -Iutilities/str
  -Iutilities/debug_console_lite
  -Icomponent/uart
  -Iboard
)

if [[ ${#EXTRA_INCLUDE_DIRS[@]:-0} -gt 0 ]]; then
  for include_dir in "${EXTRA_INCLUDE_DIRS[@]}"; do
    COMMON_FLAGS+=("-I$include_dir")
  done
fi

if [[ ${#EXTRA_DEFINES[@]:-0} -gt 0 ]]; then
  for extra_define in "${EXTRA_DEFINES[@]}"; do
    COMMON_FLAGS+=("$extra_define")
  done
fi

LINK_FLAGS=(
  -T "$LINKER_SCRIPT"
  -Wl,--gc-sections
  -Wl,--sort-section=alignment
  -Wl,--cref
  "-Wl,-Map=$MAP_FILE"
)

"$COMPILER" "${COMMON_FLAGS[@]}" "${SOURCES[@]}" "${LINK_FLAGS[@]}" -o "$OUTPUT_ELF"
"$SIZE_TOOL" "$OUTPUT_ELF"
