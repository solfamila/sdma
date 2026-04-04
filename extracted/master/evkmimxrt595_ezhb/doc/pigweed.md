# Pigweed Integration

This project includes a minimal Pigweed integration for the RT595 EVK.

It is based on the Pigweed logging style used by Antmicro's
`pigweed-mimxrt595-samples`, but adapted to this project's existing manual
Arm GCC build and semihosting workflow.

## What was integrated

- Pigweed `pw_log` public facade headers
- A local `pw_log_backend` implementation that writes through semihosting
- Build and run script support for dedicated Pigweed variants
- A Pigweed-backed I3C master variant that preserves the existing plain master

## Why semihosting instead of UART

Antmicro's samples route Pigweed output through FLEXCOMM12 UART.
In this workspace, semihosting has been the reliable debug channel, so the
Pigweed backend uses semihosting instead of FLEXCOMM12.

## Variants

- `pigweed-ram`
- `pigweed-flash`
- `master-pigweed-ram`
- `master-pigweed-flash`

## Build

```sh
./scripts/build_rt595.sh pigweed-ram
./scripts/build_rt595.sh pigweed-flash
./scripts/build_rt595.sh master-pigweed-ram
./scripts/build_rt595.sh master-pigweed-flash
```

Antmicro's sample repo uses Bazel as the top-level build entrypoint. This
project now provides a real Bazel cross-build from the repository root using
Antmicro's Pigweed fork and its Cortex-M33 arm-gcc toolchain rules.

```sh
cd ../../..
bazelisk build //:master_pigweed_ram.elf
bazelisk build //:master_pigweed_flash.elf
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

## Run

```sh
RT595_PROBE=PRASAQKQ ./scripts/run_rt595.sh pigweed-ram semihost
RT595_PROBE=PRASAQKQ ./scripts/run_rt595.sh master-pigweed-ram semihost
```

## Flash

```sh
RT595_PROBE=PRASAQKQ ./scripts/flash_rt595.sh pigweed-flash
RT595_PROBE=PRASAQKQ ./scripts/flash_rt595.sh master-pigweed-flash
```

## Bootstrap

Pigweed headers are fetched on demand by:

```sh
./scripts/bootstrap_pigweed.sh
```

The checkout is stored at `third_party/pigweed` in the workspace root and is
ignored by the repository.