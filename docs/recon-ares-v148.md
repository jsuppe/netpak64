# ares Recon Report — R1–R10 Resolved

**Recon target:** ares **v148**, commit `0aafd85789215e84e1e43415c07d4c88461b7899` (2026-05-28), checkout at `jsuppe@balthazar:~/dev/ares` (upstream clone, no fork yet).
**Checklist source:** `netpak64-ares-integration.md` §13. **Date:** 2026-07-02.
**Verdict up front: all ten assumptions confirmed — no design changes required.** The integration doc's glue design can be implemented as written.

| # | Assumption | Verdict | Evidence (file:line at v148) |
|---|---|---|---|
| R1 | Single shared PI-space decode used by both CPU access and PI DMA; insertion point ahead of dd range | ✅ Confirmed | `ares/n64/pi/bus.hpp` — templated `PI::busRead<Size>` / `PI::busWrite<Size>` is the single decode for both paths (`PI::readWord` wraps `busRead<Word>` for CPU; DMA uses `<Half>`). dd answers only `0x0500'0000–0x0500'05bf` (four sub-ranges: c2s ≤`03ff`, ds ≤`04ff`, regs ≤`057f`, ms ≤`05bf`); everything else in Dom2/Addr1 falls to `if(address <= 0x05ff'ffff) return unmapped;`. **NetPak inserts a guarded check for `0x05f0'0000–0x05f0'ffff` immediately before that unmapped fallthrough** — dd's ranges are untouched, and disabled ⇒ identical fallthrough (constraint 1 holds structurally). |
| R2 | Cart-line IRQ assert/deassert helper (CAUSE IP3) used by dd | ✅ Confirmed | `ares/n64/dd/dd.cpp:179` — `cpu.setInterruptPending(CPU::Interrupt::Cartridge, line);`. NetPak's `updateIrqLine()` calls exactly this. |
| R3 | Recurring queue-event registration pattern + event enum home | ✅ Confirmed | Enum: `ares/n64/n64.hpp:88` (`Queue::DD_Clock_Tick` et al.) — add `Queue::NetPak_Tick`. Dispatch: `ares/n64/cpu/cpu.cpp:94` (`case Queue::DD_Clock_Tick: return dd.rtc.tickClock();`) — add a `NetPak_Tick` case. Registration: `cpu.queueInsert(Queue::DD_Clock_Tick, 187'500'000)` (`dd/dd.cpp:151`) — the tick handler re-arms itself with `queueInsert`, exactly the assumed pattern. |
| R4 | Access width presented to Dom2/Addr1 handlers (16 vs 32) | ✅ Confirmed — both, exactly as designed | `busRead<Size>` static_asserts `Half` or `Word` ("PI bus will do 32-bit (CPU) or 16-bit (DMA) only"); components expose both: `ares/n64/dd/dd.hpp:77,79` — `readHalf(u32) -> u16` and `readWord(u32) -> u32`. NetPak's glue implements the same dual signature; DeviceCore stays 16-bit-native with word wrappers (integration doc §5.2 unchanged). |
| R5 | Run-ahead flag visibility inside the core | ✅ Confirmed | `ares/ares/ares.hpp:72–74` — global `atomic<bool> ares::_runAhead` with `ares::runAhead()` accessor. `tick()` checks `ares::runAhead()` to suppress mutation; unserialize checks it to skip the epoch bump/flush. |
| R6 | `Node::Setting::Boolean` system-toggle precedent | ✅ Confirmed | Pattern used throughout the N64 tree, e.g. `ares/n64/aleck64/game-config/*.hpp` (`parent->append<Node::Setting::Boolean>("Test Mode", false, callback)`). Homebrew Mode itself: `ares/n64/system/system.cpp:37` (`if(name == "Homebrew Mode") system.homebrewMode = value.boolean();`) — the gating precedent for the NetPak enable toggle. |
| R7 | Homebrew ROM-header pak-config hook (for later auto-attach) | ✅ Confirmed | `mia/medium/nintendo-64.cpp:764` — "Homebrew (libdragon / Everdrive special header flag)" handling exists. P4's auto-attach has a hook point; nothing needed for P1. |
| R8 | Component serialization registration point | ✅ Confirmed | `ares/n64/system/serialization.cpp` — `System::serialize(serializer&, bool synchronize)` aggregates component serializers; dd has its own `dd/serialization.cpp` in the same shape NetPak's `netpak/serialization.cpp` will follow. |
| R9 | CMake target/source-list mechanics | ✅ Confirmed | `ares/n64/CMakeLists.txt` — flat source list including `dd/dd.cpp` (line 14) and companions (lines 170–176). The four `netpak/*` glue files join this list, guarded by the `ARES_NETPAK` option. Build system: CMake presets (`macos` preset → Xcode generator, `build_macos/`). |
| R10 | Multi-instance friendliness | ✅ No blocker found | No single-instance lock/lockfile logic in `desktop-ui/`. Two instances run side by side (shared settings file is a theoretical write-contention caveat; P1 verifies empirically by launching two). |

## Two incidental findings worth recording

1. **ares' open-bus pattern is not `0xFFFF`.** `busRead`'s unmapped value is `(address & 0xFFFF) | (address << 16)` (address-echo pattern). This does **not** conflict with the spec (`netpak-spec.md` §3: reads at unlisted offsets *within the decoded window* return `0xFFFF` per half — that's inside our device, our choice), but the *probe* story benefits: a stock console's open-bus read of `MAGIC` returns the address-echo, which can never equal `0x4E503634` at address `0x05F0_0000` (echo would be `0x0000_05F0`-ish) — probe step 1 is even more robust than assumed.
2. **ISViewer decode precedent** sits in the same `busRead` (`cartridge.isviewer.enabled() && address >= 0x13ff'0000…`) — a guarded, enable-gated device check in the shared decode, which is precisely the shape NetPak's check takes. Cite this in the upstream RFC as in-tree precedent.

## Consequences

- **P0 recon exit criterion: met.** All R1–R10 answered with file/symbol citations against a pinned tag.
- `docs/architecture/04-component-ares-glue.md` §5 updated to reference this report (per the ROADMAP re-sync obligation).
- The glue implementation surface is now fully enumerated: `pi/bus.hpp` (2 insertion points, read+write), `n64.hpp` (enum member), `cpu/cpu.cpp` (dispatch case), `system/serialization.cpp` (1 call), `n64/CMakeLists.txt` (4 files + option), plus the new `ares/n64/netpak/` directory. That is the entire in-tree diff, consistent with the ~300–500 line budget.
