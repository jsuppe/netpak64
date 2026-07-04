# N64 Expansion-Connectivity Research Notes

**Companion to `netpak64-hardware-integration.md` (v0.2).** This file preserves the raw research findings and analysis behind the hardware design decisions — the "why we know what we know," kept separately so the design doc stays prescriptive while nothing investigated is lost. Each section notes its date, sources, and confidence level. Style follows the project's honesty convention: verified facts, indirect evidence, and open gaps are labeled as such.

---

## 1. Cartridge connector vs. EXT connector (2026-07-04)

**Question:** what does the bottom EXT expansion port carry that the top cart slot doesn't, and could either be used beyond the PI bus?

**Findings:**
- The cart connector is 48 pins (two positions unpopulated); the EXT connector is the same interface with those two extra positions **present** — pins 14/39 exist *only* on EXT.
- Both connectors sit on the **same electrical PI bus** — shared AD16 address/data, same strobes, same transfer speed. This is by design: the 64DD is simply another PI-bus device, which is why NetPak64's address space (Dom2/Addr1) is the DD's space.
- The exact function of the two EXT-only pins is **not well documented publicly** (community consensus: DD-specific — likely power/control, not a new bus). This is the one residual unknown; a multimeter trace on a real board would close it.
- Cart pins 18/43 carry the CIC↔PIF data/clock; pin 19 carries a ~1.95 MHz PIF-driven clock shared by the CIC and cart-EEPROM communication.

**Sources:** GBAtemp cartridge-connector research thread, ConsoleMods N64 connector pinouts, n64brew Game Pak / Peripheral Interface pages (some fetches 403'd; facts above are the cross-confirmed subset).

**Consequence for the design:** EXT offers nothing electrically that the cart slot doesn't, except physical coexistence with a cart (see §5). The SC64-fork cart-slot design stands.

## 2. Joybus channel architecture — can a cart-slot/EXT device touch controllers? (2026-07-04)

**Question:** can a device on the cart or EXT connector read or inject controller input?

**Verified findings (n64brew Joybus Protocol page):**
- Joybus has **5 channels**. Channels **0–3 are the four front controller ports**, on dedicated PIF wires that do not reach either the cart or EXT connector at all.
- **Channel 4 is a dedicated one-wire, half-duplex joybus line routed to the cartridge connector** (the "external joybus") — the mechanism cart-EEPROM saves use. SC64 already implements a real-time FPGA joybus responder on it (`fw/rtl/n64/n64_si.sv`, emulating EEPROM + RTC device types).
- Joybus wire protocol: **250 kbit/s**, 4 µs/bit; a controller-style poll (1-byte command, 4-byte response) ≈ 160 µs.

**Direct electrical injection into channels 0–3: NO** — three independent lines of evidence:
1. No documented signal on either connector reaches the SI/controller side; everything shared is PI-bus.
2. **Nintendo's own behavior:** the 64DD's keyboard and mouse peripherals plug into the *front controller ports*, not through the DD/EXT path. If EXT could reach the controller channels, Nintendo's own DD peripherals wouldn't have needed the front ports.
3. Architecturally, PI-bus devices are slaves — they answer reads in their window; there is no bus-master path from cart/EXT into PIF RAM or RDRAM.

**Confidence:** near-certain (the two undocumented EXT pins are the only unaccounted wires; betting odds strongly on "boring").

**What IS possible on channel 4:** a custom joybus *device* readable by software that polls channel 4 deliberately — the basis of the optional Bluetooth-controller feature (`netpak64-hardware-integration.md` §13). Only NetPak64-aware ROMs would ever read it; retail games never poll channel 4 for player input.

## 3. The software vector that does exist: GameShark-style code patching (2026-07-04)

Direct electrical input injection is closed, but influencing a **retail** game's input from the PI-bus side has a real, shipping precedent: the **GameShark** — a cart-slot *passthrough* device that hooks the boot process and patches game code/RAM per frame. "Hold button X" GameShark codes exist today: they write the game's controller-state variables in RDRAM every frame via the installed hook. Implications:

- **Cart-slot passthrough position:** code patching is proven. A device could patch a specific game's input routine to also consult an external register — per-game reverse-engineering work (every game stores input differently), but technically real.
- **EXT position:** cannot intercept a cart's ROM fetches (no passthrough), so it cannot hook a game booting from a real cart above it. The DD-style alternative — boot *from* the bottom device, then load and patch a game image — collapses into "a flashcart in the bottom port," which the cart-slot SC64 fork already is, more buildably.
- **Assessment for NetPak64:** noted as the honest answer to "can retail games be networked from this hardware," but *out of scope* — it is per-game patching work, unrelated to the spec/`netpak-core`/relay stack, and adjacent to the input-lockstep model the project explicitly positions against (see §4).

## 4. Retail game mirroring / networked sync — assessed and declined (2026-07-04)

**Question raised:** could the hardware offer networked play of unmodified retail games (input mirroring / game sync)?

**Analysis:**
- A retail cart has no knowledge of NetPak64; it reads controller ports 0–3 and renders. Networking it means either **input-lockstep netplay** (capture/inject input into identical game instances) or **remote-play video streaming** (capture A/V, stream, inject input back).
- Input-lockstep **on real hardware** lacks the tool emulator netplay depends on: savestates/rollback. Only pure input-delay netcode is possible, with a latency floor set by network RTT.
- Both models require the controller-injection capability that §2 shows doesn't exist electrically, or per-game patching (§3), or console modification (internal SI-line taps), or a separate controller-port device.
- Neither model reuses the NetPak64 stack (`netpak-spec.md`, `netpak-core`, `np64-relay`, `libnetpak`) in any meaningful way — and lockstep is precisely the approach the proposal differentiates NetPak64 *from* ("the game sees a NIC, not netplay").

**Decision: out of scope for NetPak64.** A controller-port Bluetooth dongle (8BitDo-style, real prior art) and any lockstep box are separate projects; recorded here so the assessment isn't re-litigated from scratch later.

## 5. Form factor: two viable shapes (revised 2026-07-04 — supersedes the earlier "resolved: cart-only")

An earlier revision of this section resolved the form factor as all-in-cartridge and dismissed the EXT port. That dismissal under-weighted two things surfaced in later discussion: (a) **cable/antenna ergonomics** — a bottom-mounted device keeps the wired Ethernet run and antenna fixed to the console while game cartridges swap freely above it; and (b) the bigger one, (b) **decoupling** — an EXT device is a NIC that works with *any* cart-slot ROM source (EverDrive, stock SC64, any flashcart), rather than being welded to one forked flashcart. Both connectors share the same PI bus, so the spec (`0x05F0` window) is attachment-point agnostic — **no spec or gateware-core change between shapes.**

**Physical viability (verified 2026-07-04):** the EXT port is the same connector family and transfer speed as the cart slot; community **breakout-board projects for the EXT port exist**, and the Doctor V64 era established the "base unit the console sits on, connected via EXT" form factor as shipped, real-world hardware. The connector is custom work (edge-finger PCB + shell, dust cover removed) but not unobtainable, as previously overstated.

### Shape A — all-in-cartridge (SC64 fork; the validated path)
- One device: flashcart + NIC in a cart shell. Homebrew lives on it; nothing else needed.
- Inherits SC64's shipping PCB, bus front-end, CIC-in-fabric (H2/H3/H4/H6/H7 retired), USB flashing, sim-verified gateware — the working HW-M1 plan (§11.1 of the hardware doc).
- Costs: swapping to another cartridge means unplugging the wired connection with it; the NIC is inseparable from this one flashcart; RJ45 in a cart shell is tight; net-module load rides SC64's existing 3.3 V rail (open power question).

### Shape B — EXT base unit (the decoupled NIC)
- A slim base under the console: FPGA (register file + bus front-end) + ESP32 (+ optional W5500/RJ45), answering only `0x05F0`. Any flashcart above provides the game.
- **No CIC needed at all** — the top cartridge boots the console; the base is a pure peripheral, exactly like the 64DD alongside combo carts. (H4 vanishes architecturally rather than by inheritance.)
- **Much simpler board than a flashcart fork**: no ROM emulation, no save types, no SD/USB loader — just the NIC. Generous mechanical room for RJ45, antenna placement, status LEDs, and optionally **its own power input** (sidestepping the cart-rail budget question entirely — and consistent with the 64DD-had-its-own-supply precedent, §6).
- Gateware transfers: `n64_netpak.sv` + the conformance harness are attachment-agnostic; SC64's `n64_pi.sv` bus front-end RTL is reusable under GPL on a new board (same PI waveforms arrive at EXT).
- Costs: a **new PCB** — H6 (electricals/signal integrity) and H7 (mechanical) reopen in full; custom EXT edge connector + shell; the two EXT-only pins (14/39) remain undocumented (likely ignorable for a PI device — verify); EXT port current capacity unverified if not self-powered; absent on iQue variants.

### Sequencing insight: the shapes share everything up to the PCB
The SC64-fork path remains the **cheapest validation vehicle regardless of final shape**: HW-M1/M2 on a stock SC64 (USB-flash, reversible) validates the gateware, timing (H1/H5), and firmware on real silicon — all of which transfers to a Shape-B board unchanged, since it's the same bus and the same RTL. Decision on the shipping shape can therefore be deferred until after SC64-based validation, at zero cost.

**Status: open, leaning Shape B for the shipping device** (owner preference for the decoupled/base form, 2026-07-04), with Shape A as the validation path and a legitimate shipping option in its own right.

## 6. 64DD power draw — inconclusive, with reasoning (2026-07-03)

**Question:** how much power did the 64DD draw (context: the precedent device in our slot, relevant to the cart-rail power-budget question)?

**Findings:** no trustworthy figure located. One source claimed 19 W (almost certainly conflating the console's own commonly-cited draw); another claimed the DD draws entirely through the expansion port with no separate supply — contradicted by physical evidence (64DD units have a separate power connection) and engineering logic (a spinning disk motor + head actuation is exactly the load class that historically got its own supply rather than a console bus rail). Wikipedia's article carries no wattage; n64brew fetch was blocked.

**Working conclusion:** treat the 64DD as evidence that *motorized* loads warranted separate power in this slot's era — reinforcing, not resolving, the "confirm SC64 3.3 V regulator headroom empirically" open item. Our load (ESP32 + optional W5500, no motors) is a different class, but the check stays mandatory before a PCB rev. A real number would come from a 64DD service manual/teardown or a multimeter on a real unit.

## 7. Cross-references

- Design decisions these notes feed: `netpak64-hardware-integration.md` §3.1–3.2 (SC64 fork), §7 (dual-uplink ESP32), §12 (open questions incl. power budget and form factor), §13 (joybus channel-4 feature).
- Wire diagram: `hardware-staging/wire-diagram.html` (dual-uplink, confirmed-vs-open lists).
- Verified gateware artifacts: `hardware-staging/` (probe sim + RTL↔`netpak-core` differential conformance, 96,425 checks / 0 mismatches).
