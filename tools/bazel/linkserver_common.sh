#!/usr/bin/env bash

RT595_LINKSERVER_OUTPUT=

rt595_print_linkserver_diagnostics() {
  local context=$1
  local status=$2
  local output=$3

  printf 'LinkServer failed while %s.\n' "$context" >&2
  if [[ -n "${RT595_LINKSERVER_PROBE:-}" ]]; then
    printf 'Probe: %s\n' "$RT595_LINKSERVER_PROBE" >&2
  fi
  if [[ -n "${RT595_LINKSERVER_DEVICE:-}" ]]; then
    printf 'Device: %s\n' "$RT595_LINKSERVER_DEVICE" >&2
  fi
  printf 'Exit code: %s\n' "$status" >&2

  if [[ "$output" == *"No flash configured at the specified address"* ]]; then
    cat >&2 <<'EOF'
Hint: this image is not flash-loadable at the requested address.
Use LinkServer run for RAM ELFs and LinkServer flash for flash ELFs.
EOF
    return
  fi

  if [[ "$output" == *"No probes matched"* ||
        "$output" == *"Probe serial number not found"* ]]; then
    cat >&2 <<'EOF'
Hint: LinkServer lost track of the requested probe.
Check `LinkServer probes`, reconnect the probe USB cable if needed, and use the
probe serial substring instead of a numeric probe index.
EOF
    return
  fi

  if [[ "$output" == *"Hardware interface transfer error"* ||
        "$output" == *"Could not connect to core"* ||
        "$output" == *"No connection to chip's debug port"* ]]; then
    cat >&2 <<'EOF'
Hint: the target did not respond on SWD.
Power-cycle the affected board, confirm the probe cable is seated, and rerun
the same Bazel target. Prefer probe serials like PRASAQKQ or GRA1CQLQ over
numeric probe indices when diagnosing multi-board setups.
EOF
    return
  fi

  if [[ "$output" == *"Failed to load application (stub terminated with return code 1)"* ]]; then
    cat >&2 <<'EOF'
Hint: LinkServer opened the probe but the application did not load cleanly.
If this follows a recent attach failure, power-cycle the board and rerun. For
deeper investigation, rerun the same LinkServer command with `-l 5`.
EOF
  fi
}

rt595_run_linkserver_checked() {
  local context=$1
  shift

  local log_file
  local status
  log_file=$(mktemp "${TMPDIR:-/tmp}/rt595_linkserver.XXXXXX")

  set +e
  "$@" 2>&1 | tee "$log_file"
  status=${PIPESTATUS[0]}
  set -e

  RT595_LINKSERVER_OUTPUT=$(<"$log_file")
  rm -f "$log_file"

  if (( status != 0 )); then
    rt595_print_linkserver_diagnostics "$context" "$status" "$RT595_LINKSERVER_OUTPUT"
  fi

  return "$status"
}

rt595_expect_linkserver_output() {
  local output=$1
  local pattern=$2
  local description=$3

  if ! printf '%s\n' "$output" | grep -q "$pattern"; then
    printf 'Expected %s was not found in LinkServer output.\n' "$description" >&2
    return 1
  fi
}