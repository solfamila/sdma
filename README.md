# RT595 Workspace

This repo now keeps only the active RT595 code and the Bazel workflow that drives it.

## Layout

- `src/master`: RT595 master app, shared SDK subset, linker scripts, Pigweed backend.
- `src/slave`: RT595 I3C slave app.
- `tools/bazel`: Bazel transition rules plus LinkServer run, flash, and verify wrappers.
- `.local/toolchains`: local Arm GNU toolchain used by the build.
- `.local/linkserver`: local LinkServer install used for flash and run.

Everything else at the root is Bazel workspace metadata. Bazel convenience links
are hidden with the `.bazel-` prefix so the repo root stays readable.

## Commands

```sh
bazelisk build //:master_pigweed_ram.elf //:slave_flash.elf
bazelisk run //:flash_slave_flash
bazelisk run //:run_master_pigweed_ram
bazelisk run //:verify_master_slave_transfer
```

## Fastboot

Master-only fastboot is available as a separate bootloader plus a relocated
"system" application image.

Build the bootloader and relocated application ELF files with:

```sh
bazelisk build //:master_fastboot_bootloader.elf //:master_pigweed_fastboot_app.elf
```

Build fastboot-ready padded payloads with:

```sh
bazelisk build //:master_fastboot_bootloader.fastboot //:master_pigweed_fastboot_app.fastboot
```

Flash the initial bootloader over LinkServer with:

```sh
bazelisk run //:flash_master_fastboot_bootloader
```

Do not use `//:flash_master_pigweed_fastboot_app` for initial provisioning.
The current LinkServer wrapper performs a mass erase, which removes the
bootloader region as well. Program the relocated `system` image through
fastboot after the bootloader is already running.

At boot, hold `SW1` during reset or power-up to stay in fastboot mode.
Otherwise the bootloader jumps to the relocated system app if one is present.
Once the bootloader is running, use the USB port at `J38` with standard
fastboot commands to update the `fastboot` or `system` partitions using the
generated `.fastboot` payloads. On macOS, if the device enumerates but
`fastboot getvar` or `fastboot flash` hangs, run the host tool with `sudo`.

## Compatibility Scripts

If you want the old entrypoints, use:

```sh
src/master/scripts/run_rt595.sh master-pigweed-ram semihost
src/master/scripts/run_rt595.sh slave-ram semihost
src/master/scripts/flash_rt595.sh master-pigweed-flash
src/master/scripts/flash_rt595.sh slave-flash
```

Those scripts are now thin Bazel wrappers. There is no separate manual-build path anymore.