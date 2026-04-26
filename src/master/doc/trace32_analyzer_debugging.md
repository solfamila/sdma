# TRACE32 Analyzer Debugging

This is the current workflow for tracing the RT595 master with the Lauterbach
MCP server when the external trace hardware is the off-chip `Analyzer`.

## Preconditions

- Launch TRACE32 with `.local/trace32/master-powerview.t32`.
- Connect the MCP server to the live TRACE32 session.
- If the slave ELF has not changed, do not reflash it. Start it with LinkServer
  `run` or reset it and leave the code image alone.
- Use `.local/trace32/load_master_interrupt_offchip_trace_after_clocks.cmm` to
  reset the master, load the ELF, jump to `init_status_led`, and configure the
  off-chip trace pins and `Trace.METHOD Analyzer` setup.

## MCP Trace Sequence

The reliable MCP sequence is:

1. `run_practice_script(load_master_interrupt_offchip_trace_after_clocks.cmm)`
2. `run_until("run_read_transfer")`
3. `run_command("Trace.RESet")`
4. `run_command("Trace.Init")`
5. `run_until("hold_status_led")`
6. `run_command("PRinTer.FILE \".../master_interrupt_read_trace.txt\" ASCIIE")`
7. `run_command("WinPrint.Trace.List")`

`Trace.Init` is required after `Trace.RESet` when using `Analyzer`. Clearing the
trace buffer without re-initializing the trace hardware produces an empty export
even though execution continues and the breakpoint at `hold_status_led` is hit.

## How We Reduce Trace Size

Do not capture a blind reset-to-failure run.

- First prove the high-level control flow once. In the current investigation the
  bounded write-to-failure trace showed `run_write_transfer()` returned success
  and `main()` advanced into `run_read_transfer()`.
- After that, move the start breakpoint forward to `run_read_transfer()`.
- Keep the stop breakpoint at `hold_status_led()` so the capture ends at the
  final failure latch.
- Export to a fresh file such as `.local/trace32/master_interrupt_read_trace.txt`
  instead of reusing an older multi-GB dump.
- Inspect the bounded export with targeted `rg`, `head`, `tail`, and `sed`
  slices rather than trying to open the whole file in the editor.

This drops the export from the earlier multi-GB full-run dump to a bounded file
that is practical to search from the shell.

## Current Finding

The bounded Analyzer trace shows:

- `run_write_transfer()` returns success.
- `main()` then enters `run_read_transfer()`.
- The read leg returns `7902`, which maps to `kStatus_I3C_Nak`.

That means the master-side trace is now best used to bracket the failure while
the root-cause investigation stays focused on the slave receive/echo path.