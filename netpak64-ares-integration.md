# NetPak64 × ares — Emulator Integration Design

**v0.1 · companion to netpak64-proposal.md v0.2**
Target: ares mainline (v148-era), N64 core · Device decode: PI Dom2/Addr1, base `0x05F0_0000`, 64 KiB window

---

## 1. Scope and design constraints

This document specifies how the NetPak64 virtual device is implemented inside ares: code layout, bus hookup, threading and state ownership, interrupt delivery, savestate/reset semantics, configuration, build, and test strategy. Three constraints drive every choice:

1. **Zero behavioral diff when disabled.** With NetPak off, ares must be bit-identical to upstream — decode falls through to existing dd/open-bus paths. This is what keeps the fork rebasable and the eventual upstream RFC credible.
2. **All architecturally visible device state is owned by the emulation thread.** The network thread never touches anything a savestate serializes or the CPU can read. This single invariant makes savestates race-free and the loopback configuration fully deterministic.
3. **Glue is thin; logic lives out-of-tree.** The in-ares diff is bus decode + node registration + a tick event + serialization plumbing (~300–500 lines). Everything else — register file, command FSM, rings, wire protocol — lives in `netpak-core`, a standalone library shared later with firmware and other emulators.

## 2. Where NetPak sits in ares

ares composes systems from an object tree (`ares::Node`) of systems, ports, peripherals, and settings; the frontend (`desktop-ui`, hiro-based) enumerates that tree for UI. The N64 core (`ares/n64/`) is a set of cooperatively scheduled components (`cpu`, `rsp`, `rdp`, `mi`, `vi`, `ai`, `pi`, `si`, `ri`, `cartridge`, `dd`, `controller`, …) plus a global emulated-time event `queue` used for deferred work (PI DMA completion, SI transfers, DD timing, etc.).

NetPak follows the two existing precedents:

- **ISViewer** (Homebrew Mode): a bus-mapped pseudo-device in cart space serving homebrew — the closest in-tree reference for glue patterns.
- **dd (64DD)**: the resident of Dom2/Addr1 and the existing user of the cartridge interrupt line — the reference for decode position, IRQ path, and component shape.

NetPak is a passive bus device plus one recurring queue event; it does not need its own `Thread` in ares' scheduler sense.

## 3. Code layout

```
ares fork (branch: netpak)
├── thirdparty/netpak-core/          # git submodule — all real logic
│   ├── include/np64/{device.hpp, netclient.hpp, spsc.hpp, frame.hpp}
│   ├── src/{device.cpp, cmd_fsm.cpp, rings.cpp, netclient.cpp,
│   │        backend_udp.cpp, backend_loopback.cpp, sockets_{posix,win}.cpp}
│   └── tests/                       # host unit tests, no emulator needed
└── ares/n64/netpak/
    ├── netpak.hpp                   # struct NetPak, extern netpak (mirrors dd's shape)
    ├── netpak.cpp                   # load/unload/power, tick event, config parse
    ├── io.cpp                       # readHalf/writeHalf (+ word wrappers)
    └── serialization.cpp
```

`netpak-core` is C++ matching ares' language standard, no external dependencies (BSD/winsock wrapped internally), no exceptions across the glue boundary.

## 4. The core/glue split

```cpp
namespace np64 {

// Emulation-thread owned. No threads, no sockets, no clocks of its own.
class DeviceCore {
public:
  // PI-visible surface — 16-bit native (AD16 bus), word wrappers in glue
  u16  readHalf(u32 offset);
  void writeHalf(u32 offset, u16 value);

  // Called only from the emulated-time tick event:
  // ingests frames from the net SPSC into the RX ring, emits TX,
  // steps the command FSM, recomputes IRQ level.
  void tick(SpscQueue<Frame>& fromNet, SpscQueue<Frame>& toNet,
            LinkStatus link);

  bool irqAsserted() const;          // level = |(IRQ_STATUS & IRQ_MASK)
  void reset(bool hard);
  u32  epochBump();                  // loadstate/reconnect discontinuity
  void serialize(Serializer&);       // regs, rings, cmd FSM, epoch
};

// Owns a std::thread + UDP socket. Touches ONLY the SPSC queues and
// an atomic LinkStatus snapshot. Never sees DeviceCore.
class NetClient {
public:
  struct Config { std::string relay, room, name; };  // "loopback" = builtin echo backend
  explicit NetClient(Config);
  ~NetClient();                      // joins thread, closes socket
  SpscQueue<Frame>& inbound();
  SpscQueue<Frame>& outbound();
  LinkStatus status() const;         // lock-free snapshot
  void requestFlush();               // drop in-flight after loadstate
};

}
```

The glue struct mirrors how other N64 components are declared:

```cpp
// ares/n64/netpak/netpak.hpp
struct NetPak {
  Node::Object node;
  Node::Setting::Boolean enable;     // same pattern as Expansion Pak / Homebrew Mode toggles

  auto load(Node::Object parent) -> void;   // create node, read config, construct core+net
  auto unload() -> void;
  auto power(bool reset) -> void;

  auto readHalf(u32 address) -> u16;        // offset = address - 0x05f0'0000
  auto writeHalf(u32 address, u16 data) -> void;
  auto readWord(u32 address) -> u32;        // two half accesses, matches AD16 reality
  auto writeWord(u32 address, u32 data) -> void;

  auto tick() -> void;                      // queue-event handler
  auto updateIrqLine() -> void;             // drive CPU cart line (CAUSE IP3), dd's path
  auto serialize(serializer&) -> void;

  std::unique_ptr<np64::DeviceCore> core;
  std::unique_ptr<np64::NetClient>  net;
  n1 irqLine;
};
extern NetPak netpak;
```

## 5. Bus integration

### 5.1 Decode

The N64's CPU uncached accesses and PI DMA both funnel through a shared PI-space decode (recon item R1 pins the exact function/file). NetPak inserts **ahead of** the dd range, guarded by the enable flag:

```cpp
// shared PI bus decode — illustrative
if(address >= 0x05f0'0000 && address <= 0x05f0'ffff && netpak.enable()) {
  return netpak.readHalf(address);
}
if(address >= 0x0500'0000 && address <= 0x05ff'ffff) return dd.read(address);
// … existing cart ROM / SRAM / open-bus fallthrough unchanged
```

Disabled ⇒ identical fallthrough to upstream (constraint 1). The window is 64 KiB; sub-decode inside `DeviceCore` (registers `0x0000–0x07FF`, `CMD_DATA` at `0x0800`, `TX_BUF` at `0x1000`, `RX_WIN` at `0x2000`; reads elsewhere in the window return `0xFFFF`).

### 5.2 Access width

The physical PI bus is 16 bits; the register file is therefore 16-bit-granular with 32-bit registers assembled from two halves (big-endian, high half first). Side effects (doorbells, W1C, `CMD` trigger, `RX_CONSUME`) fire on the **low half** write completing a word — meaning a 32-bit CPU store or DMA burst behaves identically to two half stores, and the semantics transfer directly to real hardware. If recon shows ares' decode hands us 32-bit accesses for this range (as it does for dd registers), the word wrappers are the entry point and the rule is preserved internally.

### 5.3 PI DMA and timing

Because decode is shared, PI DMA to/from the window works with no additional code: `TX_BUF` fills and `RX_WIN` drains via ordinary `dma_write`/`dma_read` from the ROM side. Transfer pacing comes for free too — ares models PI transfer duration from the domain timing registers, and Dom2 timings are software-programmed (`PI_BSD_DOM2_{LAT,PWD,PGS,RLS}`, written by `netpak_init()` to spec-pinned values), so emulated DMA durations track what real hardware will do without NetPak-specific timing code.

## 6. Threading and state ownership

```
 emulation thread                        │   net thread (std::thread in NetClient)
─────────────────────────────────────────┼──────────────────────────────────────────
 CPU / PI DMA reads & writes             │   UDP socket I/O (poll w/ short timeout)
   └─ DeviceCore register side effects   │   relay handshake, keepalive, control acks
 Queue::NetPak_Tick (every ~0.5 ms       │   token re-pin, RTT sampling
 emulated time ≈ 46,875 CPU cycles):     │
   drain inbound SPSC → RX ring          │◄── inbound SPSC (Frame)
   drain TX slot → outbound SPSC         │──► outbound SPSC (Frame)
   step command FSM, sample LinkStatus   │──► LinkStatus (atomic snapshot)
   recompute + drive cart IRQ line       │
```

Rules, in order of importance:

- **RX ring, TX slot, registers, FSM, epoch: emulation-thread only.** The net thread's world is two SPSC queues and one atomic status word. There is no lock anywhere on the emulation thread's path.
- **Delivery is quantized to the tick event.** Packets become architecturally visible only at tick boundaries in emulated time. Consequences: savestates taken between ticks are trivially coherent; with the loopback backend, execution is fully deterministic (same ROM ⇒ same trace), which makes CI regression runs possible.
- **Doorbell latency:** a TX doorbell enqueues to the outbound SPSC immediately (net thread picks it up in real time); only *inbound* visibility waits for a tick. Worst-case added local latency ≈ one tick (0.5 ms) — noise against network RTT. Tick period is a build-time constant, revisitable.
- The tick handler re-arms itself via the queue; registration follows the existing recurring-event pattern (recon R3).

## 7. Interrupt delivery

`IRQ_STATUS` bits (`RX_READY`, `TX_DONE`, `EVENT`, `LINK_CHANGE`) are set inside `tick()` and by register side effects (W1C on status, writes to mask). The line is level-triggered: `irqLine = |(IRQ_STATUS & IRQ_MASK)`, recomputed on any contributing change, and driven onto the CPU's cartridge interrupt input — CAUSE bit IP3, the same line and the same assert/deassert path dd uses (recon R2 pins the exact helper). Because line updates happen only on the emulation thread (rule above), there is no cross-thread IRQ race by construction. On the ROM side this pairs with libdragon's cart-line handler registration, with `netpak_poll()` as the fallback pump.

## 8. Reset, savestates, run-ahead

**power(reset = true)** (soft reset): registers and rings clear, relay session *survives* (NetClient untouched), epoch bumps, `LINK_CHANGE` pends once the ROM re-enables the mask. A reset mid-match should be a resync event, not a disconnect.

**power(reset = false)** (hard power / new game): NetClient destroyed and reconstructed from config — fresh handshake, new token.

**serialize()**: blob version, register file, RX ring (indices + contents), TX slot, command FSM state, epoch, `irqLine`. **unserialize()**: restore, then `epoch++`, `net->requestFlush()` (drop stale in-flight frames), recompute IRQ line. The relay session deliberately lives outside the savestate; `netpak_epoch()` is how ROMs detect the discontinuity and resync.

**Run-ahead / rewind:** unsupported while NetPak is enabled — same operational stance users already accept for 64DD. Concretely: if the core's run-ahead flag is set (recon R5), `tick()` suppresses all mutation and unserialize skips the epoch bump/flush, so the internal save/load storm doesn't shred device state; the glue logs one warning telling the user to disable run-ahead. Rewind is refused (or warned) at enable time.

## 9. Configuration plumbing

v1 keeps configuration out of the GUI entirely — essential anyway for running two instances on one machine:

```
NP64_RELAY   relay host:port, or "loopback" (builtin echo backend, no network)
NP64_ROOM    join code ("" = create)
NP64_NAME    node identity
NP64_LOG     trace level for the netpak subsystem
```

Precedence: environment > optional `netpak.bml` beside the ROM > defaults. The enable toggle itself is a `Node::Setting::Boolean` on the N64 system node (the Expansion Pak / Homebrew Mode pattern), so it appears in the existing settings UI for free. Later phases: a proper settings pane (hiro), and auto-attach via the advanced homebrew ROM-header mechanism ares already uses to configure controller paks for homebrew titles (hook in mia's N64 loader — recon R7).

## 10. Build integration

`netpak-core` enters as a submodule under `thirdparty/` and is added to ares' CMake build as a static library linked into the ares core target, guarded by an option (`-DARES_NETPAK=ON`, default on in the fork). No new external dependencies; sockets come from the platform (winsock2 / BSD). The four glue files join the N64 core's source list. Fork hygiene: one feature branch, rebased per upstream release; the in-tree diff stays small enough that rebases are mechanical.

## 11. Testing and determinism

Three layers, cheapest first:

1. **Host unit tests (netpak-core, no emulator):** register FSM golden tests (probe sequence, W1C, doorbell edge cases, ring wrap/overflow → `RX_COUNT` saturation and drop-oldest policy), command FSM (illegal opcodes, busy re-entry), protocol soak against a simulated lossy/reordering link (control-plane retransmit, token re-pin).
2. **Deterministic in-emulator tests:** `NP64_RELAY=loopback` + the `echo` test ROM; scripted ares run asserts on ISViewer/stderr output. Same-input-same-trace determinism (per §6) makes this CI-able.
3. **Two-instance integration:** scripted launch of two ares processes + `np64-relay --dev` on localhost; `chat` ROM exchange verified via debug output; packet-loss and NAT-rebind soaks live at the relay layer's own tests.

A fourth artifact falls out for free: a **headless conformance harness** that drives `DeviceCore` with scripted PI accesses — the executable definition of the spec that adapter firmware will later be tested against.

## 12. Upstream posture

The RFC story to ares maintainers, when the spec stabilizes: gated behind Homebrew Mode; zero diff when disabled; deterministic under loopback (testable); serves the same audience as ISViewer with the same shape (bus-mapped homebrew device); spec versioned outside the emulator. Until then, the fork publishes builds per ares release; HailToDodongo's N64-only dev fork remains a candidate second home if mainline declines.

## 13. Recon checklist (P0/P1 spike — confirm exact symbols before coding)

| # | Assumption to confirm | Where to look |
|---|---|---|
| R1 | Single shared PI-space decode used by both CPU uncached access and PI DMA; insertion point ahead of dd range | `ares/n64/pi/`, dd read/write call sites |
| R2 | Cart-line IRQ assert/deassert helper (CAUSE IP3) used by dd | `ares/n64/dd/`, `ares/n64/cpu/` |
| R3 | Recurring queue-event registration pattern + event enum home | `queue` usage: PI DMA finish, SI, DD clock events |
| R4 | Access width presented to Dom2/Addr1 handlers (16 vs 32) | dd register read/write signatures |
| R5 | Run-ahead flag visibility inside the core | how AV-skip/run-ahead is plumbed to systems |
| R6 | `Node::Setting::Boolean` system-toggle precedent | Expansion Pak / Homebrew Mode setting code |
| R7 | Homebrew ROM-header pak-config hook (for later auto-attach) | `mia/` N64 loader (v146 feature) |
| R8 | Component serialization registration point | `ares/n64/system/serialization.cpp` (or equivalent) |
| R9 | CMake target/source-list mechanics post build-system migration | root + `ares/` CMake files |
| R10 | Multi-instance friendliness (settings/lock files) | desktop-ui startup path |

## 14. Implementation order (maps to proposal P1)

1. Recon spike filling §13 (≈½ day).
2. `netpak-core` skeleton + host unit tests green (registers, loopback backend, SPSC).
3. Glue: decode + enable setting + env config → `probe` ROM detects the device in ares.
4. Tick event + rings + IRQ line → `echo` ROM green against loopback, IRQ and poll modes.
5. Serialization + reset semantics → savestate round-trip test green.
6. Swap loopback for `backend_udp` against `np64-relay --dev` → P2 begins.
