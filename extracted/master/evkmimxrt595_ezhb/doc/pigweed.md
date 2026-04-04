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
project now provides Bazel wrapper targets in the same style for the Pigweed
master variants:

```sh
cd extracted/master/evkmimxrt595_ezhb
bazelisk run //:build_master_pigweed_ram
bazelisk run //:build_master_pigweed_flash
```

These Bazel targets currently wrap the existing manual GCC build scripts rather
than replacing them with a full mcuxpresso/Pigweed Bazel target port.

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