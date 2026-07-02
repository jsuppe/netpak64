# 03 — netpak-core Components (C4 Level 3)

Diagram source: [`diagrams/c4-component-netpak-core.puml`](diagrams/c4-component-netpak-core.puml)

`netpak-core` is the project's center of gravity: a standalone, dependency-light C++ library holding **all real device logic**, consumed as a `thirdparty/` git submodule by the ares fork today and by adapter firmware and other emulators (cen64, gopher64 via FFI) later. It has no threads of its own on the emulation side, no exceptions across the glue boundary, and no external dependencies (sockets wrapped internally).

## 1. The two halves and the seam between them

The library is two classes with intentionally asymmetric worlds:

- **`DeviceCore`** — *emulation-thread owned.* Register file, command FSM, rings, IRQ model, epoch, serialization. No threads, no sockets, **no clocks of its own** — time only advances when the host calls `tick()`. This is the half that lifts into MCU firmware nearly verbatim (an MCU main loop calls `tick()` exactly as ares' event queue does).
- **`NetClient`** — *owns a `std::thread` and a UDP socket.* Handshake, keepalive, control-plane retransmission, token re-pin, RTT sampling. Touches **only** the two SPSC queues and an atomic `LinkStatus` snapshot. Never sees `DeviceCore`.

The seam — two `SpscQueue<Frame>` and one atomic — is the entire cross-thread contract. Everything race-sensitive about this project reduces to preserving that seam.

## 2. RegisterFile: the spec made executable

16-bit-granular storage (the PI bus is AD16; see primer §6.1) presenting the spec §5.2 map: `MAGIC`/`VERSION`/`CAPS`/`STATUS`, `IRQ_STATUS` (W1C) / `IRQ_MASK`, the `CMD`/`CMD_STATUS`/`ARG*`/`RES*` mailbox, TX/RX descriptor registers, and the three windows (`CMD_DATA` 512 B at `0x0800`, `TX_BUF` 2 KiB at `0x1000`, `RX_WIN` at `0x2000`). Reads elsewhere in the 64 KiB window return `0xFFFF` per half.

**The side-effect rule** (integration doc §5.2, restated because it's subtle and firmware-critical): 32-bit registers are two big-endian halves; **side effects fire when the low half of a word completes**. A 32-bit CPU store, a DMA burst, and two sequential 16-bit stores are therefore indistinguishable to the device — on the emulator *and* on a future CPLD front-end. Golden tests pin this (half-then-half, word, DMA-burst orderings all yield identical FSM transitions).

## 3. CommandFSM: the mailbox

Commands (`SESSION_CREATE`, `SESSION_JOIN(code)`, `LIST_PEERS`, `PING`, `GET_TIME`, …) execute asynchronously: `CMD` write → `CMD_STATUS=busy` → FSM emits control frames via the outbound SPSC → responses ingested at later `tick()`s → `CMD_STATUS=ok/errno`, results in `RES0–3`, `EVENT` IRQ. Illegal opcodes and busy re-entry are defined errors (host unit tests). Variable-length parameters (room codes, peer lists) go through `CMD_DATA`.

Design intent: the ROM never blocks. `libnetpak` polls `CMD_STATUS` or waits on the `EVENT` interrupt; a command in flight coexists with data traffic.

## 4. Rings: single-packet TX, exposed-head RX

- **TxSlot**: one in-flight packet. `TX_BUF` filled (PI DMA or CPU stores) → `TX_LEN`/`TX_DST` → `TX_DOORBELL`. The doorbell enqueues to the outbound SPSC **immediately** (not at the next tick) so local send latency doesn't quantize; `TX_DONE` then reports the slot free.
- **RxRing**: device-side ring whose *head packet only* is visible through `RX_WIN`; `RX_CONSUME` pops and exposes the next; `RX_COUNT` reports depth (saturating). Overflow policy: **drop-oldest** — for game state, the newest packet is worth more than the oldest (the same reasoning that leads action games to prefer unreliable transports; cf. [Gaffer On Games, "UDP vs. TCP"](https://gafferongames.com/post/udp_vs_tcp/)).

Why this shape: address decode stays trivial (a window and a couple of registers — CPLD-friendly), while ring depth, retransmit bookkeeping, and relay state stay on the device side where the implementation can afford them. The ROM needs exactly one DMA per packet in each direction — important because PI DMA is a single, system-wide contended resource (primer §6.4).

## 5. NetClient: the only place wall-clock time exists

The net thread polls its socket with a short timeout and services: the **HELLO/WELCOME** handshake (obtaining the 32-bit token carried on every subsequent frame), **reliable control delivery** (seq/ack, RTO ≈ 250 ms doubling, small window — the [N64-NetLib](https://github.com/buu342/N64-NetLib)/[GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)-style pattern, implemented once here and shared by emulator and firmware), **keepalives** (~10 s), and **RTT sampling** feeding `GET_TIME`/`GET_STATS`. `requestFlush()` (called by glue after loadstate) drops in-flight frames so a restored state doesn't receive pre-snapshot traffic.

Backends are pluggable behind one interface: `backend_udp` (real relay) and `backend_loopback` (builtin echo, zero sockets). Loopback is not a toy — it is the **determinism anchor**: with it, `DeviceCore` consumes no external nondeterminism at all, giving same-ROM-same-trace CI runs (integration doc §11).

## 6. Testing: three layers plus the conformance harness

1. **Host unit tests** (no emulator): register golden tests (probe sequence, W1C, doorbell edges, ring wrap/overflow), FSM error paths, protocol soak against a simulated lossy/reordering link.
2. **Deterministic in-emulator**: loopback + `echo` ROM, scripted ares, assert on ISViewer/stderr.
3. **Two-instance integration**: two ares + `np64-relay --dev`, `chat` ROM.

The fourth artifact falls out for free and is strategically the most important: a **headless conformance harness** driving `DeviceCore` with scripted PI accesses — the *executable definition of the spec*, which adapter firmware will later be tested against. It is what keeps "same spec, three implementations" honest.
