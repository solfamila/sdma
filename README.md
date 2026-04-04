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

## Compatibility Scripts

If you want the old entrypoints, use:

```sh
src/master/scripts/run_rt595.sh master-pigweed-ram semihost
src/master/scripts/run_rt595.sh slave-ram semihost
src/master/scripts/flash_rt595.sh master-pigweed-flash
src/master/scripts/flash_rt595.sh slave-flash
```

Those scripts are now thin Bazel wrappers. There is no separate manual-build path anymore.