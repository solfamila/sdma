# Pigweed Integration

This workspace keeps the active RT595 sources under `src/`:

- `src/master` contains the master app and the shared RT595 SDK subset.
- `src/slave` contains the I3C slave app.
- `tools/bazel` contains the only supported build, run, flash, and verify wrappers.

## What was integrated

- Pigweed `pw_log` public facade headers
- A local `pw_log_backend` implementation that writes through semihosting
- A Pigweed-backed I3C master variant used by the end-to-end master/slave flow

## Why semihosting instead of UART

Antmicro's samples route Pigweed output through FLEXCOMM12 UART.
In this workspace, semihosting has been the reliable debug channel, so the
Pigweed backend uses semihosting instead of FLEXCOMM12.

## Build

Antmicro's sample repo uses Bazel as the top-level build entrypoint. This
project now provides a real Bazel cross-build from the repository root using
Antmicro's Pigweed fork and its Cortex-M33 arm-gcc toolchain rules.

```sh
cd ../../..
bazelisk build //:master_pigweed_ram.elf
bazelisk build //:master_pigweed_flash.elf
bazelisk build //:slave_ram.elf
bazelisk build //:slave_flash.elf
```

The full workflow also has Bazel run and flash entrypoints:

```sh
bazelisk run //:flash_slave_flash
bazelisk run //:run_master_pigweed_ram
bazelisk run //:flash_master_pigweed_flash
bazelisk run //:verify_master_slave_transfer
```

Defaults assume the known-good probe mapping used in bring-up:

- `PRASAQKQ` for the master board
- `GRA1CQLQ` for the slave board

You can override probes or LinkServer paths with environment variables such as
`RT595_PROBE`, `RT595_MASTER_PROBE`, `RT595_SLAVE_PROBE`, `RT595_DEVICE`, and
`LINKSERVER_BIN`.

## Script Wrappers

The compatibility wrappers in `src/master/scripts` now delegate to the repo-root
Bazel targets. Only the active master/slave variants are still supported.

## Run

```sh
RT595_PROBE=PRASAQKQ ./scripts/run_rt595.sh master-pigweed-ram semihost
RT595_PROBE=GRA1CQLQ ./scripts/run_rt595.sh slave-ram semihost
```

## Flash

```sh
RT595_PROBE=PRASAQKQ ./scripts/flash_rt595.sh master-pigweed-flash
RT595_PROBE=GRA1CQLQ ./scripts/flash_rt595.sh slave-flash
```