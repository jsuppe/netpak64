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

**The precedent to mine:** [SummerCart64](https://github.com/Polprzewodnikowy/SummerCart64) is fully open (hardware, ECP5 gateware, RP2040 firmware, CIC emulation) and *already interfaces Dom2/Addr1* for its 64DD emulation. It is the single most valuable reference and the fastest starting substrate. EverDrive-64 and 64drive are closed but establish that FPGA-class bus interfacing is the norm.

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

### 7.2 Two candidate BOMs
- **Design A — SC64-derived, Wi-Fi (fastest to "it works").** Lattice ECP5 (bus front-end + register file; reuse SC64 gateware structure) · RP2040 (orchestration + CIC emulation, runs `netpak-core`) · ESP32-C3 (Wi-Fi/UDP) over SPI. Leans on the most existing, open, N64-proven work.
- **Design B — minimal, open-toolchain, wired (best for validation).** Lattice iCE40 UP5K (bus front-end + register file; fully open yosys/nextpnr flow) · RP2040 (CIC + `netpak-core`) · W5500 (wired hardware UDP). Wired + open-source FPGA flow makes it the cleanest platform to *characterize the bus* and retire H1/H2/H5 with the fewest confounds.

Recommendation: prototype on **Design B** to validate the bus and timing against silicon with minimum variables, then move to **Design A** for the untethered, demo-able unit.

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
| **H2** | AD16 latch order, strobe timing, burst/page auto-increment | Gateware RTL correctness | n64brew + SC64 gateware; scope confirm |
| **H3** | Cart-IRQ connector pin + libdragon `register_CART_handler` fires on device assertion | IRQ path | Minimal FPGA that pulses IP3; ROM handler counts it |
| **H4** | CIC variant / emulation needed to boot | Booting at all | SC64 CIC firmware; try boot with/without |
| **H5** | Real PI DMA presents halves high-then-low with expected increment (validates low-half side-effect rule) | Side-effect model | DMA a known pattern to `TX_BUF`; scope the half sequence |
| **H6** | Electricals: 3.3 V, bus loading, drive strength, series termination | Reliability | Signal-integrity bring-up |
| **H7** | Physical attach point (cart slot vs. DD connector) + real-DD mutual exclusion | Form factor | Mechanical + bus arbitration check |
| **H8** | Reset / NMI line behavior at the connector → `reset()`/epoch | Discontinuity handling | Scope reset; confirm epoch bump path |
| **H9** | Achievable read cadence / page-mode behavior at chosen Dom2 timing | Throughput model | Sustained-read benchmark ROM |
| **H10** | Max sustained TX/RX throughput vs. the ≤1 KB / 10–30 Hz design center and relay 0.4 MB/s room math | Design-center validity | `netbench` extended, on silicon |

## 11. Bring-up order (mirrors emulator P1 → P2)

Each milestone reuses an *existing, unmodified* ROM — that reuse is the proof.

1. **HW-M1 — bus bring-up / MAGIC.** FPGA answers only `MAGIC` (+VERSION, +a CMD_DATA scratch) in the `0x05F0` window. The **existing `probe.z64`** prints `NP64_PROBE: DETECTED`. Retires H1–H4, H6. (= emulator "probe detects the device".)
2. **HW-M2 — register file + rings + loopback.** Port `DeviceCore` to the MCU behind the gateware shadow; wire the loopback backend on the MCU. The **existing `echo.z64`** runs green in both poll and IRQ modes on hardware loopback — no network yet. Retires H5, H8, H9. (= emulator echo milestone.)
3. **HW-M3 — network + relay.** Add the net chip + socket HAL; point at a real `np64-relay`. **`netchat`/`netbench`/`netpong` unchanged** run over real hardware to the same relay a second player can join from an emulator. Retires H10; validates the whole thesis. (= emulator P2.)
4. **HW-M4 — CIC, enclosure, conformance.** Robust CIC, mechanical, and the §9 conformance ROM asserting silicon == spec. Candidate for a spec **v1.1** if any H-item forces a change (e.g. measured Dom2 timing replaces the provisional values).

A NetPak64 hardware unit and an emulator instance **playing `netpong` in the same relay room** is the P4 headline demo — and it needs no new game code, because there isn't any to write.

## 12. Open questions
- **CIC licensing/approach** — emulate (open UltraCIC-style) vs. harvest; affects manufacturability and legality.
- **Attach form factor** — top cart slot (simplest, blocks the game cart — so this is a *networked-homebrew-only* cart, which is the actual use case) vs. a pass-through. A NetPak64 unit is itself the cartridge running a `libnetpak` game; it does not sit alongside another cart. Worth stating in the proposal's non-goals.
- **Single-MCU vs. split** — can one ESP32 be both `netpak-core` host and Wi-Fi stack, with only a CPLD (not full FPGA) for the bus front-end? Depends on whether a CPLD-scale part can hold the register-file shadow + meet H2 timing. Attractive for cost; gated on H1/H2.
- **Provisional Dom2 values** — H1 may let them go *faster* (more throughput) or force them slower; either way the spec §2.1 table gets replaced with measured numbers and the PROVISIONAL marker cleared.

---

*Companion documents: `netpak-spec.md` (normative register/wire contract), `netpak64-ares-integration.md` (emulator realization), `netpak64-proposal.md` (rationale, phases, non-goals). This doc is the P4 hardware track; P0–P3 (emulator, driver, relay, reference game) are complete and provide the frozen spec and full software stack this design ports.*
