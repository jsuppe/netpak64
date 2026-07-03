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

## Status: skeleton, not yet built or simulated

This is HW-M1 Phase A step 3 material — a starting point to review against
SC64's actual `n64_dd.sv` and then simulate. **Two things to confirm from
`n64_dd.sv` before building** (both flagged inline):
1. Exact clock/reset port names.
2. Whether `reg_bus.address` is byte- or halfword-addressed (`ADDR_IS_BYTES`
   in `n64_netpak.sv`). A one-line sim of the probe sequence settles it.

## Build / test path (per §11.1)

Simulate first (extend SC64's `fw/tests`, or iverilog/Verilator) — green sim =
known-good bitstream *before* it touches your SC64. Then build via SC64's
Diamond/Docker flow and flash over USB (`sc64deployer firmware update`, after a
`firmware backup`). No hardware mod; reversible.

## Licensing

GPL-3.0-or-later — this is SC64-derivative gateware. The portable device logic
(`netpak-core`) stays ISC and standalone and is never merged in here; only the
thin bus-facing block lives in the fork.
