# 04 — ares Glue Components (C4 Level 3)

Diagram source: [`diagrams/c4-component-ares-glue.puml`](diagrams/c4-component-ares-glue.puml)

The glue is deliberately the *least* interesting part of the system — four files, ~300–500 lines, every one of them replaceable per ares release. Its design is governed by the three constraints of integration doc §1; this doc explains how each component honors them and where it plugs into ares.

## 1. How ares is put together (context for non-ares readers)

[ares](https://github.com/ares-emulator/ares) composes emulated systems from an object tree (`ares::Node`) of systems, ports, peripherals, and settings; the frontend (`desktop-ui`, built on the hiro UI library) renders that tree. The N64 core is a set of cooperatively scheduled components (`cpu`, `rsp`, `rdp`, `mi`, `vi`, `ai`, `pi`, `si`, `ri`, `cartridge`, `dd`, …) plus a global **emulated-time event queue** for deferred work (PI DMA completion, SI transfers, DD timing). NetPak is *not* a scheduler `Thread` — it is a passive bus device plus one recurring queue event, exactly like the profile of a simple memory-mapped peripheral.

Two in-tree precedents shape every choice: **ISViewer** (a bus-mapped homebrew pseudo-device — the glue-pattern reference) and **dd**, the 64DD (the Dom2/Addr1 resident and cart-IRQ user — the decode-position, IRQ-path, and component-shape reference).

## 2. Components

**Node + enable setting.** A `Node::Object` at the 64DD's attach point (NetPak and DD are mutually exclusive — they decode the same region; proposal §5.1 accepts this). The enable toggle is a `Node::Setting::Boolean` following the Expansion Pak / Homebrew Mode pattern, so it surfaces in ares' existing settings UI with no frontend work (recon R6).

**PI I/O handlers.** Decode inserts **ahead of** the dd range in the shared PI-space decode used by both CPU uncached access and PI DMA (recon R1 confirms the exact function):

```cpp
if(address >= 0x05f0'0000 && address <= 0x05f0'ffff && netpak.enable()) {
  return netpak.readHalf(address);
}
if(address >= 0x0500'0000 && address <= 0x05ff'ffff) return dd.read(address);
// existing fallthrough unchanged
```

Disabled ⇒ **bit-identical fallthrough to upstream** — constraint 1, the property that keeps rebases mechanical and the upstream RFC credible. Because the decode is shared, PI DMA to `TX_BUF`/`RX_WIN` works with zero additional code, and ares' modeling of Dom2 timing registers means DMA pacing tracks the spec-pinned values `netpak_init()` programs (integration doc §5.3).

**Tick event.** The heart of the determinism story. Every ~0.5 ms of *emulated* time (~46,875 CPU cycles; build-time constant), the event drains the inbound SPSC into the RX ring, moves any doorbelled TX to the outbound SPSC, steps the FSM, samples `LinkStatus`, recomputes the IRQ level, and re-arms itself (recon R3 for the registration pattern). Consequences worth spelling out:

- Inbound packets become architecturally visible **only at tick boundaries in emulated time** — savestates taken between ticks are trivially coherent, and loopback runs are fully deterministic.
- Doorbell latency is asymmetric by design: TX enqueues immediately (real time), only *inbound visibility* quantizes. Worst-case added local latency ≈ one tick — noise against network RTT.

**IRQ line driver.** `irqLine = |(IRQ_STATUS & IRQ_MASK)`, recomputed on any contributing change (tick ingest, W1C write, mask write), driven onto the CPU's cart interrupt input — CAUSE IP3, the same assert/deassert helper dd uses (recon R2). Line updates happen only on the emulation thread, so there is **no cross-thread IRQ race by construction** — a theorem, not a test.

**Serialization.** Savestate = blob version + `DeviceCore::serialize` (registers, rings, FSM, epoch) + `irqLine`. The relay session deliberately lives *outside* the savestate. Unserialize: restore → `epoch++` → `NetClient::requestFlush()` → recompute line; the epoch is how ROMs detect the discontinuity (`netpak_epoch()`) and resync at the game level. **Run-ahead/rewind:** unsupported while attached (the 64DD precedent users already accept). Concretely: if the core's run-ahead flag is set (recon R5), `tick()` suppresses all mutation and unserialize skips the epoch bump/flush so run-ahead's internal save/load storm doesn't shred device state; one warning is logged.

## 3. Reset semantics

| Event | DeviceCore | NetClient | Rationale |
|---|---|---|---|
| Soft reset (`power(true)`) | Registers/rings clear, epoch++ | **Survives untouched** | A console reset mid-match should be a resync, not a disconnect |
| Hard power (`power(false)`) | Fresh | Destroyed and reconstructed — fresh handshake, new token | New game, new identity |

## 4. Build & fork hygiene

`netpak-core` enters as a submodule under `thirdparty/`, built as a static library behind `-DARES_NETPAK=ON` (default on in the fork, recon R9 for CMake mechanics post ares' build-system migration). One feature branch, rebased per upstream release (ares ships fast — v148-era as of mid-2026). The litmus test for any proposed glue change: *would this line survive being rewritten from scratch after a bad rebase?* If it embodies logic rather than plumbing, it belongs in `netpak-core`.

## 5. Recon checklist status

**✅ Resolved 2026-07-02 against ares v148 (`0aafd8578921`)** — all ten assumptions confirmed, no design changes required. Full citations in [`docs/recon-ares-v148.md`](../recon-ares-v148.md). Key confirmed symbols:

| Item | Confirmed symbol |
|---|---|
| R1 decode | `PI::busRead<Size>`/`busWrite<Size>` in `ares/n64/pi/bus.hpp`; insert before the `≤ 0x05ff'ffff → unmapped` fallthrough |
| R2 IRQ | `cpu.setInterruptPending(CPU::Interrupt::Cartridge, line)` (`dd/dd.cpp:179`) |
| R3 tick | `Queue::` enum in `n64.hpp:88`, dispatch in `cpu/cpu.cpp:94`, re-arm via `cpu.queueInsert` |
| R4 width | dd exposes both `readHalf(u32)->u16` and `readWord(u32)->u32` (`dd/dd.hpp:77,79`) — glue mirrors this |
| R5 run-ahead | `ares::runAhead()` — `atomic<bool>` in `ares/ares/ares.hpp:72` |
| R6 toggle | `Node::Setting::Boolean` precedent + Homebrew Mode gate (`system/system.cpp:37`) |
| R8 savestate | `System::serialize(serializer&, bool)` in `n64/system/serialization.cpp` |
| R9 build | flat source list in `ares/n64/CMakeLists.txt` |
