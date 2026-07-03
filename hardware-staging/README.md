# hardware-staging — SC64 fork sources (pre-fork)

The gateware for the physical NetPak64 adapter, staged in the hub until the
SummerCart64 fork (`sc64-netpak`) exists — same pattern as `glue-staging/` for
the ares glue. See `netpak64-hardware-integration.md` (§3.2 fork plan, §11.1
HW-M1 breakdown) for the full design.

## Contents

- **`n64_netpak.sv`** — HW-M1 minimal block: answers MAGIC / VERSION / a
  CMD_DATA scratch in the `0x05F0` window, enough for `probe.z64` to report
  `DETECTED` on a real console. Modeled on SC64's `n64_dd.sv` peripheral shape,
  connects via the `n64_reg_bus.netpak` modport. Written against the **real**
  `n64_reg_bus` interface (16-bit granular, per-peripheral select + rdata mux).
- **`n64_reg_bus.additions.md`** — exact add-only edits to SC64's
  `fw/rtl/n64/n64_reg_bus.sv` (declare `netpak_select`/`netpak_rdata`, add the
  modport + read mux term) plus where to drive `netpak_select` in the PI decode.
- **`tb_n64_netpak.sv`** + **`run_sim.sh`** — the Phase A step-5 probe
  simulation and its runner.

## Status: skeleton, logic SIMULATED GREEN (not yet on hardware)

`tb_n64_netpak.sv` drives the full spec §2.2 probe (read MAGIC → read VERSION →
write+read CMD_DATA, + an unmapped-reads-0xFFFF check) against the block and
asserts `NP64_PROBE: DETECTED`. **Verified on Verilator 5.050, `TB: PASS` in
BOTH addressing conventions** (`ADDR_IS_BYTES` = 1 and 0) — so the register/
probe logic is correct regardless of which SC64 turns out to use.

```
$ ./run_sim.sh
NP64 probe sim (ADDR_IS_BYTES=1):
  ok:   MAGIC == 'NP64'
  ok:   VERSION major == 0
  ok:   CMD_DATA reads back written pattern
  ok:   unmapped offset reads 0xFFFF
  ok:   CMD_DATA second cell independent
NP64_PROBE: DETECTED
TB: PASS
```

**Still to confirm from `n64_dd.sv` at integration** (neither is a block-logic
risk — the sim proved the block; these are wiring facts):
1. Exact clock/reset port names.
2. Which addressing convention SC64's reg bus uses → set `ADDR_IS_BYTES`
   accordingly (both proven to work).

## Differential conformance: RTL vs netpak-core — PASSING

`conformance/{diff_harness.cpp, run_diff.sh}` is the executable conformance
harness the ROADMAP promised, extended to a third implementation: it drives
identical 16-bit accesses into the **Verilated RTL** and **np64::DeviceCore**
(the ISC reference behind the ares device) in lockstep and diffs every read.

```
CONFORMANCE (RTL vs netpak-core, skeleton scope): 96425 checks, 0 mismatches
DIFF: PASS
```

Scope is masked to what the skeleton implements (MAGIC/VERSION/CMD_DATA +
unmapped gaps); at HW-M2 the mask widens to the full register map and this
same binary becomes the gateware's acceptance test. Run: copy netpak-core
(include/, src/) to `./netpak-core`, then `./conformance/run_diff.sh`.

Sim-portability notes learned the hard way: Verilator silently ignores modport
EXPRESSIONS (the stub now uses plain modports with driver-side gating; SC64's
real interface keeps its expressions and Diamond handles them), and a comment
line may not begin with the word "verilator" (parsed as a pragma).

## Build / test path (per hardware-integration §11.1)

Sim is done (above). Next: drop the block + `n64_reg_bus.additions.md` into an
SC64 fork, build the bitstream via SC64's Diamond/Docker flow, back up stock
firmware (`sc64deployer firmware backup`), flash over USB
(`sc64deployer firmware update`), and confirm the unmodified `probe.z64`
prints `DETECTED` on a real console. No hardware mod; reversible.

## Licensing

GPL-3.0-or-later — this is SC64-derivative gateware. The portable device logic
(`netpak-core`) stays ISC and standalone and is never merged in here; only the
thin bus-facing block lives in the fork.
