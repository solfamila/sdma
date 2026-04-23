load("@rules_platform//platform_data:defs.bzl", "platform_data")


def mimxrt595_binary(name = "", binary = "", testonly = False):
    return platform_data(
        name = name,
        target = binary,
        testonly = testonly,
        platform = "//tools/bazel:mimxrt595_platform",
    )


def mimxrt595_fastboot_binary(name = "", binary = "", testonly = False):
    return platform_data(
        name = name,
        target = binary,
        testonly = testonly,
        platform = "//tools/bazel:mimxrt595_fastboot_bootloader_platform",
    )