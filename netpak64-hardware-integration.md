# NetPak64 × Hardware — Physical Adapter Design

**v0.1 · companion to netpak64-proposal.md v0.2 and netpak64-ares-integration.md v0.1**
Target: a physical N64 cartridge-slot network adapter · Device decode: PI Dom2/Addr1, base `0x05F0_0000`, 64 KiB window · Status: **P4 design study — nothing built or silicon-validated yet**

---

## 0. Reading order and honesty note

This is a *design study*, not a validated design. Everything below is reasoned from the frozen register/wire spec (`netpak-spec.md`), the emulator integration (`netpak64-ares-integration.md`), and public N64 hardware knowledge (n64brew wiki, the open-source SummerCart64 project). **No claim here has touched silicon.** Where a value or behavior is only validated against ares' PI model, it is marked with ⚠ and appears in the §10 validation checklist. The purpose of the emulator-first program was to arrive here with the spec frozen and the entire software stack (`netpak-core`, `libnetpak`, `np64-relay`, test ROMs) already proven — so this becomes a *port*, not a clean sheet.

## 1. Scope and design constraints

How the NetPak64 device is realized as physical hardware: bus interface, the gateware/firmware partition, interrupt and reset electricals, the network subsystem, a bill-of-materials sketch, the silicon-validation checklist, and a bring-up order that mirrors the emulator's P1. Constraints — the first three carry over verbatim from the emulator design, which is the point:

1. **The register-level spec is the contract, unchanged.** The device presents the exact `netpak-spec.md` register map at `0x05F0_0000` over the PI bus. Success is defined as *the existing, unmodified test ROMs (`probe`, `echo`, `netpong`) run green against silicon.* No ROM or driver change is permitted to make hardware work; if hardware needs a change, it is a spec bug, fixed in the spec.
2. **Architecturally visible state is owned by the bus-facing side.** On the emulator this was "the emulation thread owns the register file; the net thread touches only two SPSC queues + one atomic." On hardware this becomes a physical partition (§3): the **gateware** owns the register file the bus reads; the **network MCU** touches only the queue/mailbox boundary. The invariant that made savestates race-free is the same invariant that lets bus reads never stall on the network stack.
3. **Logic lives in `netpak-core`.** The command FSM, rings, and wire protocol run as MCU firmware built from the *same* `netpak-core` source that is the emulator's reference implementation and that passes 116k host-test checks. The hardware-specific code is the bus front-end (gateware) and the thin HAL under `netpak-core`'s backend interface.
4. **Hardware-specific:** the adapter must satisfy the console's boot lockout (CIC, §7.3) and meet PI Dom2 bus timing (§5.3) — two concerns the emulator never had.

## 2. Where NetPak sits on real hardware

The N64 cartridge connector carries the **PI (Peripheral Interface) bus**, aka the cart bus: a 16-bit multiplexed address/data bus (**AD16**), 3.3 V, with separate address-latch and read/write strobes (§5.1). The same bus reaches the 64DD expansion connector — Domain 2 / Address 1 is the 64DD's space, and NetPak64 occupies the top of it (`0x05F0_0000`, chosen high to clear the real DD ASIC and SC64's DD-emulation range near `0x0500_05xx`).

**Consequences that were always hardware-motivated:**
- NetPak64 and a real 64DD are **mutually exclusive** — the adapter answers where the DD would. On a stock console with no DD, that range is open bus, which is why the probe sequence (§spec 2.2) is a three-step handshake, not a bare MAGIC read.
- The device asserts the **cartridge interrupt line** (surfaced as CAUSE IP3) — the same line the 64DD drives. ⚠ exact connector pin + assertion electricals are validation item H3.

**The precedent to mine:** [SummerCart64](https://github.com/Polprzewodnikowy/SummerCart64) (SC64) is fully open-source (GPL-3.0; Lattice **MachXO2** FPGA gateware built with Lattice Diamond, CIC emulation *in fabric*) and *already interfaces Dom2/Addr1* for its 64DD emulation. It is not just the best reference — it is the recommended substrate to **fork** (§3.1, §3.2). EverDrive-64 and 64drive are closed but establish that FPGA-class bus interfacing is the norm.

## 3. The partition: bus-facing gateware + network MCU

This is the hardware analog of the emulator's core/glue split, and it falls directly out of constraint 2.

```
   N64 cart bus (AD16, 3.3V)
            │
   ┌────────▼─────────┐        ┌──────────────────┐        ┌───────────────┐
   │  FPGA / CPLD     │  queue │   Network MCU     │  UDP   │  np64-relay    │
   │  ── bus front ── │◄──────►│  netpak-core      │◄──────►│  (unchanged)   │
   │  AD16 demux,     │ (SPI/  │  DeviceCore FSM,  │  (Wi-Fi │                │
   │  Dom2 timing,    │  paral)│  rings, NetClient │  or Eth)│                │
   │  register-file   │        │  + thin socket    │        └───────────────┘
   │  shadow, IP3 drv │        │  HAL              │
   └──────────────────┘        └──────────────────┘
        owns bus-visible            owns network;
        register state              never in the bus read path
```

**Recommended sub-architecture (i): gateware holds the register file.** The FPGA holds a dual-port register file / ring-head shadow; the bus side answers reads combinationally within the Dom2 window; the MCU updates that shadow across the queue boundary at its own pace. Bus reads *never* wait on the MCU or the network. This maps 1:1 onto `DeviceCore` (bus-visible state) vs. `NetClient` (queues + atomic) — the code already draws this line.

Rejected sub-architecture (ii): thin bus↔SPI bridge with the MCU holding all state. The MCU cannot pre-stage read responses fast enough for arbitrary reads within the bus window; it puts the network stack in the latency path. Only viable if the bus is run at maximally slow Dom2 timing *and* every read is to a pre-published register — too fragile. Use (i).

**Where the side-effect model pays off:** the spec's rule — *side effects (doorbell, W1C, `CMD` trigger, `RX_CONSUME`) fire when the low half of a word completes* — was written for exactly this bus. A 32-bit CPU store and a 16-bit PI DMA burst both present as two sequential half-writes; the gateware fires the side effect on the second (low) half regardless of origin. ⚠ that real PI DMA presents halves in high-then-low order with the expected auto-increment is validation item H5.

## 3.1 Preferred substrate: fork SummerCart64 (assessed 2026-07-03)

Research into [SummerCart64](https://github.com/Polprzewodnikowy/SummerCart64) (SC64) changes the recommendation from "build from parts" to **"fork SC64."** It is a fully open-source N64 flashcart whose architecture already solves most of §5–§7 and several §10 checklist items. Findings from its docs:

- **License: GPL-3.0**, whole project (gateware + firmware + hardware). GPLv3 explicitly permits manufacture and sale (Kaico Labs already sells assembled units). Implication for our licensing (see §12): a NetPak64 SC64-derivative's **gateware + firmware fork is GPL-3.0**; `netpak-core` stays **independently ISC and hardware-agnostic** and is *imported* into the GPLv3 firmware (ISC → GPL is compatible; the copyleft boundary sits at the fork repo, never inside `netpak-core`). Keep SC64 code out of `netpak-core`; keep `netpak-core` standalone so firmware/other emulators can still use it under ISC.
- **Bus decode is exactly our window shape.** SC64's memory-map doc states *"PI interface checks only upper 16 bits of address"* — i.e. decode granularity is 64 KiB windows. Our device is defined as precisely one such window (`0x05F0_xxxx`, upper-16 = `0x05F0`). Adding NetPak64 to SC64 gateware ≈ **adding one decode case** that routes `0x05F0` to a new register-file block. This is the "trivial decode" the spec was designed around, made real.
- **It already occupies our address space.** SC64's 64DD emulation lives in `0x0500_0000–0x0600_0000` (Dom2/Addr1). Its DD registers sit near the *low* end; we sit at the *top* (`0x05F0`), consistent with the spec's clearance rationale and with NetPak↔DD mutual exclusion. A NetPak-SC64 uses that range for NetPak instead of (or mode-switched with) DD.
- **The FPGA↔MCU boundary primitive already exists.** SC64 exposes a control-register block and a shared *"64DD/MCU buffer"* (1 KiB) between FPGA fabric and its microcontroller. That is exactly the hardware realization of our queue/mailbox boundary (§3) — `DeviceCore`'s rings map onto an FPGA-held buffer the MCU services, the same pattern SC64 already runs for DD.
- **CIC is solved.** SC64 moved CIC emulation *into FPGA fabric*. Adopting SC64 **retires H4 entirely** — the one boot concern the emulator never faced is inherited done.
- **Parts / toolchain** (from the repo; ⚠ confirm exact part suffix from the KiCad/interactive BOM in `sc64-extra-*.zip`, `hw/pcb`): Lattice **MachXO2** FPGA (`fw/project/lcmxo2`), 64 MiB SDRAM, 16 MiB QSPI flash, FTDI USB, microSD. Gateware is built with **Lattice Diamond** (proprietary; a free MAC-tied 1-year license), via SC64's provided `./docker_build.sh` flow — **not** the yosys/nextpnr open flow (that was my earlier error; MachXO2 is a Diamond target). The RTL is open-source (GPL-3.0); the *build tool* is not FOSS.

**What forking SC64 retires from the §10 checklist:** H2 (AD16 protocol — implemented in fabric), H3 (cart-IRQ path — it drives cart behavior on real consoles), H4 (CIC — solved in fabric), H6 (electricals — a shipping PCB), H7 (form factor — it *is* a cart); and it provides a **real-silicon-validated baseline for H1** (its DD timing works on stock consoles today). That leaves the genuinely NetPak-specific work:

1. **Gateware**: a NetPak register-file block at `0x05F0` — fork SC64's DD block, reshape to `netpak-spec.md` §3, wire the low-half side-effect rule and the IP3 driver.
2. **Firmware**: `netpak-core` (`DeviceCore` + `NetClient`) on SC64's MCU, behind the shared-buffer boundary.
3. **The one real hardware gap — networking.** SC64 has **no network interface.** A fork needs a PCB revision adding a net chip (ESP32 for Wi-Fi, or W5500 for wired) hung off the FPGA or MCU. (A bring-up shortcut is to tunnel over SC64's existing USB to a host bridge — but that reintroduces the PC tether the whole project rejects, so it is a lab step only, never the product.)

Net: SC64 shrinks the hardware effort from "interface the N64 bus" (the hard, risky part) to "add a register block + a network chip to a working, open cart." Recommended path is **fork SC64**; the from-parts Designs A/B in §7.2 remain as fallback if a fork proves impractical.

## 3.2 The fork plan (concrete, against SC64's real gateware)

SC64's N64-side gateware lives in `fw/rtl/n64/` as per-peripheral SystemVerilog blocks hung off a shared register bus — the exact structure a NetPak block slots into:

| SC64 module | Role | What the fork does |
|---|---|---|
| `n64_pi.sv` | PI/cart bus front-end (AD16 latch, strobes, timing) | **Reuse unchanged** — this is the hard, silicon-proven bus interface we want for free |
| `n64_reg_bus.sv` | Address decode / routing to peripheral blocks | **Add one route:** upper-16 == `0x05F0` → the new NetPak block |
| `n64_dd.sv` | 64DD emulation: presents registers + a buffer to the N64, serviced by firmware over the shared control bus | **Fork → `n64_netpak.sv`** — the ideal template: DD *already* is "bus-facing register file + buffer + firmware servicing + cart-IRQ," which is structurally identical to NetPak's register file + rings + doorbell + IP3 |
| `n64_scb.sv` | System Control Bus — how firmware/soft-core reaches the blocks | **Reuse** — the NetPak block's non-bus side connects here (the hardware form of our queue boundary) |
| `n64_cic.sv` | CIC in fabric | **Reuse unchanged** — H4 solved |
| `n64_cfg.sv` | SC64 control registers | Add a mode bit (NetPak vs. DD) if coexistence is wanted; else build-time select |
| `fw/project/lcmxo2` | Lattice Diamond project | Add `n64_netpak.sv`, rebuild bitstream via `./docker_build.sh` |

**`n64_netpak.sv` (the one new gateware block)** implements `netpak-spec.md` §3: the register file (16-bit granular, high-half-first word assembly), the low-half side-effect rule (§3 of this doc), `RX_WIN`/`TX_BUF` buffer windows backed by SC64's existing buffer/SDRAM plumbing, and the level-triggered IP3 assertion (fork DD's existing cart-interrupt path in `n64_dd.sv`). It is a *thin* register file + FIFO bridge — **no protocol logic in gateware.**

**Where `netpak-core` runs — the one real design fork.** SC64's firmware core is sized for flashcart servicing, not a UDP stack, and (⚠ to confirm) may be a small soft core. The **low-risk recommendation** is therefore *not* to run `netpak-core` on SC64's existing core, but on the **added network module**:

- **MachXO2 (`n64_netpak.sv`)**: bus-facing register file + ring-head shadow + IP3 + a FIFO bridge — that's all.
- **Added network module (ESP32 — 240 MHz dual-core + Wi-Fi, or RP2040+W5500)**: runs `netpak-core` (`DeviceCore` FSM + rings + `NetClient`), bridged to `n64_netpak.sv` over SPI (or SC64's SCB). This *is* the §3 partition — FPGA owns bus-visible state, the module owns network — and it keeps the gateware change minimal and the SERV-is-slow risk off the table.

An optimization to evaluate later (if SC64's core has headroom): collapse the network module into SC64's existing compute. Default to the separate module; don't gate the prototype on that analysis.

**Mode / mutual exclusion.** NetPak64 and DD share the `0x0500–0x0600` range and are mutually exclusive by design. Simplest first cut: a **build-time** select (NetPak *or* DD bitstream). A `n64_cfg` mode bit for runtime switching is a nice-to-have, not needed for the prototype.

**Bitstream build.** Lattice Diamond via SC64's Docker flow (free MAC-tied license). The gateware delta is one new `.sv` block + one decode line — small, and it composes with SC64's existing verification/test harness (`fw/tests`).

**The one PCB change.** SC64 has no network interface, so a board revision (or a bring-up bodge onto a dev unit's headers) adds the ESP32/W5500 and wires SPI to the FPGA. Everything else — bus, CIC, SDRAM, power, form factor — is inherited.

**Fork repo shape** (keeps the GPL/ISC boundary clean, §12):
```
sc64-netpak/                 # GPL-3.0 fork of SummerCart64
├── fw/rtl/n64/n64_netpak.sv     # new block (forked from n64_dd.sv)
├── fw/rtl/n64/n64_reg_bus.sv    # +1 decode route
├── net-module/                  # ESP32/RP2040 firmware: imports netpak-core (ISC) + socket HAL
└── hw/                          # PCB rev adding the net chip
netpak-core/                 # unchanged, ISC, standalone — imported, never forked-in
```

## 4. What runs where

| Function | Home | Notes |
|---|---|---|
| AD16 demux, ALE latching, /RD /WR strobes, burst auto-increment | Gateware | Timing-critical; pure logic |
| Dom2 timing compliance (LAT/PWD/PGS/RLS) | Gateware | Software programs the values; gateware honors them (§5.3) |
| Register file + RX ring head exposed at `RX_WIN` | Gateware (shadow) | Dual-port; MCU-writable |
| Cart IRQ (IP3) line driver | Gateware | Level = `|(IRQ_STATUS & IRQ_MASK)` |
| Command FSM, full RX/TX rings, epoch, stats | MCU (`netpak-core` `DeviceCore`) | Same source as emulator |
| Relay handshake, token, keepalive, ch-mux | MCU (`netpak-core` `NetClient`) | Same source |
| Reliable `ch1` | **ROM-side (`libnetpak`), unchanged** | Deliberately end-to-end so hardware needs nothing |
| CIC boot-lockout response | Small MCU / dedicated core (§7.3) | Not NetPak logic; a console-boot requirement |
| Wire protocol / relay | `np64-relay`, unchanged | Same binary, same rooms |

The only genuinely new software is a thin socket HAL implementing `netpak-core`'s backend interface on the chosen net chip (lwIP/ESP-IDF sockets, or the W5500's hardware sockets). `backend_udp.cpp` already defines the shape.

## 5. Bus interface (the hard part)

### 5.1 AD16 protocol
The PI bus multiplexes a 16-bit address/data path. A transaction latches the high address half then the low address half (ALE_H / ALE_L strobes), after which 16-bit words transfer on /RD or /WR strobes, **auto-incrementing** within a page for bursts. The gateware must: latch both address halves, compare the high bits against the `0x05F0` window, and for a hit drive (read) or capture (write) each 16-bit word with correct setup/hold against the strobes. ⚠ exact strobe timing, latch order, and burst/page semantics are H2 — confirm against n64brew + SC64 gateware before RTL.

### 5.2 Access width
16-bit native, matching the spec's AD16-granular register model exactly. 32-bit registers are two halves, big-endian, high first; the gateware assembles/splits them and applies the low-half side-effect rule (§3). No 8-bit path exists on this bus.

### 5.3 Dom2 timing ⚠ (the #1 risk to retire)
`netpak_init()` programs `PI_BSD_DOM2_{LAT,PWD,PGS,RLS}` to the spec's **provisional, maximally-conservative** values (`0xFF/0xFF/0x0F/0x03`). This is a deliberate hardware gift: the device requests the *slowest legal* bus, buying the gateware maximum slack, at a throughput cost that is fine for the ≤1 KB / 10–30 Hz design center. The provisional values must be validated and ideally *measured* on silicon (H1). The whole reason they are flagged PROVISIONAL in the spec is this moment.

## 6. Interrupt delivery
The gateware drives the cartridge interrupt line, level-triggered: asserted whenever `(IRQ_STATUS & IRQ_MASK) != 0`, deasserted when the ROM clears the latched sources via W1C. This mirrors the emulator's `updateIrqLine()` exactly. ⚠ the connector pin and whether libdragon's `register_CART_handler` fires from a device-driven assertion (vs. only the DD) is H3 — the highest-value early electrical test after H1.

## 7. Network + boot subsystems

### 7.1 Net chip options
| Option | Pros | Cons | Best for |
|---|---|---|---|
| **ESP32 / ESP32-C3** | Wi-Fi + full IP stack on one chip; can *be* the MCU (runs `netpak-core` + net) | Wi-Fi latency/jitter; RF layout | Untethered play; fewest parts |
| **W5500 (WIZnet)** over SPI | Hardware UDP sockets, deterministic, no host stack | Wired Ethernet only | **First bring-up** — removes Wi-Fi as a variable during bus validation |
| **RP2040 + ESP32** (SC64 pattern) | Reuses SC64's proven MCU + CIC firmware base | Two MCUs | Fastest path if starting from SC64 |

### 7.2 Path options (SC64 fork is preferred — see §3.1)
- **Preferred — fork SC64 + add a net chip.** Inherit the bus front-end, CIC, PCB, and form factor; add a NetPak gateware block + a network module (ESP32 Wi-Fi or W5500 wired). Smallest new-work surface; retires H2/H3/H4/H6/H7 and baselines H1. Gateware+firmware fork is GPL-3.0; `netpak-core` stays ISC (§3.1, §12).
- **From-parts fallback A — larger FPGA, Wi-Fi.** Lattice ECP5 (bus + register file; more headroom than SC64's MachXO2 if a soft core must also run `netpak-core`) · MCU for orchestration + CIC · ESP32-C3 (Wi-Fi/UDP). Only if a direct fork proves impractical.
- **From-parts fallback B — minimal, fully-open-toolchain, wired.** Lattice iCE40 UP5K (open **yosys/nextpnr** flow — genuinely FOSS, unlike SC64's Diamond dependency) · RP2040 (CIC + `netpak-core`) · W5500 (wired UDP). A clean bench for characterizing the bus (H1/H2/H5) from scratch, and the answer if the **Diamond-license or GPL-3.0 dependency is a dealbreaker** — but SC64 already gives a real-silicon-validated baseline, so this is only worthwhile if the fork path is rejected.

Recommendation: **fork SC64.** Use fallback B only as a bus-characterization bench if needed.

### 7.3 CIC (console boot lockout) — a real requirement the emulator never had
Every genuine N64 cartridge contains a CIC lockout chip; the console's PIF runs a security handshake with it at boot and **halts if it fails.** A hardware adapter must present a compatible CIC response (a matching CIC variant, or an emulator such as the open UltraCIC-family implementations — SC64 emulates CIC on its RP2040). This is orthogonal to NetPak logic but is a hard gate on *booting at all*. ⚠ which CIC variant / emulation approach is H4.

## 8. Reset, power, and the (absent) savestate
Hardware has no savestates — the emulator's hardest determinism problem simply doesn't exist here. What remains:
- **Console reset / NMI** maps to `DeviceCore::reset()`; the relay session lives outside device state (as it does in the emulator), so the game observes an **epoch bump** and resyncs at the app level — the exact `netpak_epoch()` path already exercised by the `echo` soak and used by `netpong`/`chtest`. ⚠ reset/NMI line behavior at the connector is H8.
- **Hot-plug / power sequencing**: the adapter powers from the cart slot; net-side link loss is surfaced through `STATUS.LINK_UP` + a `LINK_CHANGE` IRQ, already in the register model.
- **Run-ahead / rewind** were emulator-only concerns; not applicable.

## 9. Conformance: silicon must equal the spec
`netpak-core`'s host tests and the deterministic loopback backend define golden behavior; the register-level spec *is* the conformance target. A **hardware conformance ROM** (extend `echo`/`chtest`) drives the register/side-effect/IRQ surface and asserts silicon matches spec — the physical analog of the in-emulator regression tests. Because the same `netpak-core` runs as firmware, divergence points to either the gateware front-end or a spec assumption that silicon breaks (i.e. a finding worth a spec revision).

## 10. Silicon-validation checklist (H1–H10 — retire before/at bring-up)

Mirrors the emulator recon (integration §13, R1–R10). Each item is a physical measurement or confirmation; none can be closed against ares alone.

| # | Item | Retires | How |
|---|---|---|---|
| **H1** | Dom2 bus timing on real silicon; validate/measure LAT/PWD/PGS/RLS | PROVISIONAL spec §2.1 | Logic-analyzer capture of a real Dom2 access (SC64 in DD mode, or bus tap) |
| **H2** | AD16 latch order, strobe timing, burst/page auto-increment | Gateware RTL correctness | **Retired by SC64 fork** (in fabric); else n64brew + scope |
| **H3** | Cart-IRQ connector pin + libdragon `register_CART_handler` fires on device assertion | IRQ path | **Largely retired by SC64 fork**; confirm SC64 can drive IP3 |
| **H4** | CIC variant / emulation needed to boot | Booting at all | **Retired by SC64 fork** (CIC in fabric); else SC64 CIC firmware |
| **H5** | Real PI DMA presents halves high-then-low with expected increment (validates low-half side-effect rule) | Side-effect model | DMA a known pattern to `TX_BUF`; scope the half sequence |
| **H6** | Electricals: 3.3 V, bus loading, drive strength, series termination | Reliability | **Retired by SC64 PCB** (+ net-chip addition) |
| **H7** | Physical attach point (cart slot vs. DD connector) + real-DD mutual exclusion | Form factor | **Retired by SC64 fork** (it is a cart) |
| **H8** | Reset / NMI line behavior at the connector → `reset()`/epoch | Discontinuity handling | Scope reset; confirm epoch bump path |
| **H9** | Achievable read cadence / page-mode behavior at chosen Dom2 timing | Throughput model | Sustained-read benchmark ROM |
| **H10** | Max sustained TX/RX throughput vs. the ≤1 KB / 10–30 Hz design center and relay 0.4 MB/s room math | Design-center validity | `netbench` extended, on silicon |

## 11. Bring-up order (mirrors emulator P1 → P2)

Each milestone reuses an *existing, unmodified* ROM — that reuse is the proof. Assuming the SC64-fork path (§3.2):

1. **HW-M1 — `n64_netpak.sv` answers MAGIC.** Fork `n64_dd.sv` → a minimal `n64_netpak.sv` answering `MAGIC`/`VERSION`/a `CMD_DATA` scratch; add the `0x05F0` route in `n64_reg_bus.sv`; build via Diamond/Docker; flash an SC64 unit (or MachXO2 dev board). The **existing `probe.z64`** prints `NP64_PROBE: DETECTED` on a real console. Retires H1 (real-silicon timing baseline), H5, H8, H9 for the register path; H2/H3/H4/H6/H7 inherited from SC64.
2. **HW-M2 — full register file + rings + loopback.** Complete `n64_netpak.sv` (all registers, IP3, buffer windows + FIFO bridge). Run `netpak-core` on the net module with its loopback backend (module echoes locally). The **existing `echo.z64`** runs green in both poll and IRQ modes on a real console — no network yet.
3. **HW-M3 — network + relay.** Net module's socket HAL points at a real `np64-relay`. **`netchat`/`netbench`/`netpong` unchanged** run on real hardware against the same relay an emulator instance joins. Retires H10; validates the whole thesis — **a real N64 and an emulator playing `netpong` in one room.**
4. **HW-M4 — mode switch, enclosure, conformance.** DD/NetPak mode handling, mechanical, and the §9 conformance ROM asserting silicon == spec. Candidate for spec **v1.1** if any H-item forces a change (e.g. measured Dom2 timing replaces the provisional §2.1 values).

A NetPak64 hardware unit and an emulator instance **playing `netpong` in the same relay room** is the P4 headline demo — and it needs no new game code, because there isn't any to write.

## 11.1 HW-M1 task breakdown (the cheap first step on an SC64)

Goal: a forked `n64_netpak.sv` answers the probe in the `0x05F0` window, and the **unmodified `probe.z64`** prints `NP64_PROBE: DETECTED` on a real N64 via an existing SummerCart64. **No hardware modification, no soldering, no damage** — the FPGA bitstream reaches the SC64 through its normal USB firmware-update path (`sc64deployer firmware update`), and is fully reversible via `firmware backup`/restore. The risk is a *bad* bitstream (bricking has no documented USB recovery), which the simulate-before-flash order below is designed to eliminate.

**Phase A — RTL + simulation (no hardware touched, zero risk):**
1. Fork SC64 → `sc64-netpak`; stand up its Diamond build (`./docker_build.sh`; obtain the free MAC-tied Lattice license).
2. Read the block-interface contract in `n64_pi.sv`, `n64_reg_bus.sv`, `n64_scb.sv`, and especially `n64_dd.sv` (the template): how a peripheral block receives address/read/write/data and drives the cart IRQ.
3. Write minimal `n64_netpak.sv`: in the `0x05F0` window return `MAGIC=0x4E503634`, `VERSION=0`, and a small `CMD_DATA` scratch RAM that reads back writes — 16-bit granular, big-endian, high-half-first (spec §3).
4. Add the `0x05F0` decode route in `n64_reg_bus.sv`; instantiate the block in `n64_top.sv`.
5. **Simulate** the exact probe sequence (read MAGIC → read VERSION → write+read `CMD_DATA`) in a testbench (extend `fw/tests`; Diamond sim, or open iverilog/Verilator). Green sim = the bitstream is known-good *before* it touches the cart. This is the whole risk mitigation.

**Phase B — flash + real console (USB, reversible, no mod):**
6. `sc64deployer firmware backup sc64-stock-backup.bin` — your restore point.
7. `./docker_build.sh release` → forked `sc64-firmware-{ver}.bin` (bundles the new bitstream). ⚠ confirm the FPGA bitstream is carried in this package and that self-built (unsigned) firmware flashes — expected yes (you build the release yourself), but verify on first run.
8. `sc64deployer firmware update sc64-firmware-{ver}.bin` — USB only.
9. Load `probe.z64` via the SC64's normal ROM path; confirm `NP64_PROBE: DETECTED` on screen / ISViewer. **HW-M1 done — real bus, real silicon (baselines H1).**
10. `sc64deployer firmware update sc64-stock-backup.bin` — SC64 is a normal flashcart again.

**Why not a dev board:** a bare MachXO2 dev board is freely reflashable but is **not wired to the N64 cart bus**, so it cannot run `probe.z64` on a real console without you first building a cart-edge interface (~50-pin breakout/donor-cart wiring) — strictly *more* work than USB-flashing the SC64 you already own. The dev board's only edge is a zero-stakes flashing sandbox, which Phase-A simulation covers better. Use the SC64.

**Confirm before Phase B:** SC64 hardware revision (bitstream must match your board), that the release `.bin` carries + flashes the FPGA bitstream, and the Lattice Diamond license.

## 12. Open questions
- **Licensing split (SC64 fork)** — adopting SC64 (§3.1) makes the hardware **gateware + firmware GPL-3.0** (SC64's license; a derivative must match). `netpak-core`, `libnetpak`, `np64-relay`, and the spec stay **ISC** and standalone; `netpak-core` is imported into the GPLv3 firmware (compatible). Decision to ratify: accept this dual posture (ISC portable core + GPL-3.0 hardware fork) — the pragmatic and legal reading. Do **not** pull SC64 code into `netpak-core`.
- **CIC** — retired if forking SC64 (CIC emulation is in its FPGA fabric). Only an open question on a from-parts build.
- **Attach form factor** — top cart slot (simplest, blocks the game cart — so this is a *networked-homebrew-only* cart, which is the actual use case) vs. a pass-through. A NetPak64 unit is itself the cartridge running a `libnetpak` game; it does not sit alongside another cart. Worth stating in the proposal's non-goals.
- **Single-MCU vs. split** — can one ESP32 be both `netpak-core` host and Wi-Fi stack, with only a CPLD (not full FPGA) for the bus front-end? Depends on whether a CPLD-scale part can hold the register-file shadow + meet H2 timing. Attractive for cost; gated on H1/H2.
- **Provisional Dom2 values** — H1 may let them go *faster* (more throughput) or force them slower; either way the spec §2.1 table gets replaced with measured numbers and the PROVISIONAL marker cleared.

---

*Companion documents: `netpak-spec.md` (normative register/wire contract), `netpak64-ares-integration.md` (emulator realization), `netpak64-proposal.md` (rationale, phases, non-goals). This doc is the P4 hardware track; P0–P3 (emulator, driver, relay, reference game) are complete and provide the frozen spec and full software stack this design ports.*
