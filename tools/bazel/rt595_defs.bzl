RT595_COMMON_COPTS = [
    "-std=gnu99",
    "-ffunction-sections",
    "-fdata-sections",
    "-fno-builtin",
    "-fno-common",
    "-g3",
    "-Og",
    "-Wno-error",
]

RT595_COMMON_DEFINES = [
    "CPU_MIMXRT595SFFOC",
    "CPU_MIMXRT595SFFOC_cm33",
    "MCUXPRESSO_SDK",
    "BOOT_HEADER_ENABLE=1",
    "FSL_SDK_DRIVER_QUICK_ACCESS_ENABLE=1",
    "SDK_DEBUGCONSOLE=1",
    "MCUX_META_BUILD",
    "MIMXRT595S_cm33_SERIES",
    "CR_INTEGER_PRINTF",
    "PRINTF_FLOAT_ENABLE=0",
    "__MCUXPRESSO",
    "__USE_CMSIS",
    "DEBUG",
]

RT595_MASTER_COPTS = RT595_COMMON_COPTS + [
    "-include",
    "source/mcux_config.h",
    "-include",
    "source/mcuxsdk_version.h",
]

RT595_SLAVE_COPTS = RT595_COMMON_COPTS + [
    "-include",
    "mcux_config.h",
    "-include",
    "mcuxsdk_version.h",
]

RT595_MASTER_INCLUDES = [
    ".",
    "source",
    "flash_config",
    "CMSIS",
    "CMSIS/m-profile",
    "device",
    "device/periph",
    "drivers",
    "drivers/rt500",
    "utilities",
    "utilities/str",
    "utilities/debug_console_lite",
    "component/uart",
    "board",
]

RT595_PIGWEED_DEFINES = [
    "PW_LOG_LEVEL_DEFAULT=PW_LOG_LEVEL_INFO",
]

RT595_LINKOPTS = [
    "-Wl,--gc-sections",
    "-Wl,--sort-section=alignment",
    "-Wl,--cref",
]