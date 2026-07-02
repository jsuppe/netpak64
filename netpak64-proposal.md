# NetPak64 — A Virtual Network Expansion Device for N64 Homebrew

**Proposal v0.2** · Working title provisional · July 2026
**Decisions locked this rev:** Dom2/Addr1 (64DD-style EXT) decode · emulator-first spec · UDP-first relay

---

## 1. Summary

NetPak64 is a memory-mapped network device for the Nintendo 64, defined by a single versioned hardware specification and implemented first as a **virtual peripheral inside the ares emulator**, with a matching **libdragon extension library** that homebrew ROMs link against, and an **intermediary relay service** that carries traffic between instances. Because the contract is a register-level spec on the PI bus rather than an emulator API, the same ROM binary and the same driver library can later run unmodified against a physical adapter, enabling emulator ↔ emulator play now and emulator ↔ console play once hardware follows the spec.

The design philosophy: **the game sees a NIC, not netplay.** This is device-level networking, not input-lockstep netplay (gopher64/simple64 style). Games written against the library implement their own netcode (state sync, lockstep, or prediction) — which is exactly what you want for custom ROM development, since you control both endpoints.

## 2. Goals and non-goals

**Goals**

- A stable, versioned register-level device spec, emulator-first but hardware-plausible (trivial address decode, no behavior an MCU/FPGA front end couldn't honor), with `VERSION`/`CAPS` negotiation from day one so a later hardware-driven revision doesn't strand ROMs.
- A libdragon-friendly C library (`libnetpak`) with a small, non-blocking, game-loop-shaped API: session join/create, datagram send/recv, peer discovery, RTT/clock sync.
- A self-hostable relay service speaking a compact UDP protocol: rooms with join codes, star-topology forwarding, cheap operation (one small VPS covers many rooms).
- Zero PC-side tether: unlike USB-bridged approaches, an ares instance is a complete network endpoint on its own.
- Dev ergonomics: two ares instances + a local relay (or built-in loopback) on one machine is the standard test loop.

**Non-goals (v1)**

- Retrofitting commercial ROMs, TCP/IP stacks on the N64, P2P/NAT traversal (relay-only for v1), rollback netcode in the library, DTLS/encryption, and web/browser clients. All are possible later; none gate v1.

## 3. Prior art and positioning

- **buu342's N64-NetLib** connects homebrew to the internet through the flashcart's USB port, with a PC client app acting as the tether/packet distributor and Java game servers. It proves demand and validates a UDP-with-reliability design, but every player needs a flashcart + USB + PC bridge, and there is no emulator-native device. NetPak64 removes the tether and makes the emulator a first-class endpoint. (github.com/buu342/N64-NetLib)
- **gopher64 / simple64 netplay** synchronize controller inputs across identical deterministic instances. Great for retail games; wrong tool for homebrew that wants real netcode, variable topologies, or emulator↔hardware play.
- **ares Homebrew Mode + ISViewer** is the precedent that matters: ares already emulates a development-era memory-mapped device (ISViewer debug channel, cart-space window near `0x13FF_0000`) specifically to serve homebrew. NetPak64 follows the identical pattern — a bidirectional one.
- **HailToDodongo's ares-64 fork** (N64-only, SDL3/imgui, debugger-focused) demonstrates that maintaining a developer-oriented ares fork is viable, and is a candidate integration target alongside mainline.
- **Historical hardware** (64DD Randnet modem, SharkWire, Morita Shogi 64 modem) establishes that networked N64 devices are period-authentic; NetPak64 even lives in the same address space the 64DD did.

## 4. System architecture

```
┌────────────── ares (per player) ──────────────┐
│  N64 core                                     │
│  ┌──────────┐   PI bus reads/writes/DMA       │
│  │ Game ROM │──► NetPak device model          │
│  │(libdragon│    (registers + pkt rings)      │
│  │+libnetpak│◄── cart IRQ line                │
│  └──────────┘        │ SPSC queues            │
│                ┌─────▼──────┐                 │
│                │ net client │  (own thread)   │
└────────────────┴─────┬──────┴─────────────────┘
                       │ UDP + session token
                 ┌─────▼─────┐   (DTLS later)
                 │ np64-relay │  rooms, forwarding,
                 └─────┬─────┘  clock, presence
                       │
        other ares instances … and later, the
        physical adapter's firmware (same spec,
        same relay, same ROM)
```

Four deliverables, one contract:

1. **The spec** (`netpak-spec.md`, versioned): register map, PI timing values, IRQ semantics, command set, wire protocol. This is the keystone; everything else is an implementation of it.
2. **ares integration**: device model + thin glue in a maintained fork.
3. **`libnetpak`**: the libdragon-side driver and game-facing API.
4. **`np64-relay`**: the intermediary service.

The device model itself is built as a standalone, dependency-light C++ library (`netpak-core`: register file, ring buffers, command state machine, UDP relay client) with the ares patch as a thin adapter. This makes the core unit-testable without an emulator, portable to other emulators (gopher64 is Rust but FFI-able; cen64 is C), and directly reusable as the reference implementation for adapter firmware when that phase arrives.

## 5. Device specification (strawman v0)

### 5.1 Bus mapping — decided: PI Domain 2 / Address 1 (64DD-style EXT space)

The device decodes in `0x0500_0000–0x05FF_FFFF` — the range the 64DD occupies and the natural decode for the bottom EXT port. Since the cart port and EXT port carry the same multiplexed AD16 PI bus, this remains a spec-level choice, not a connector commitment: a future passthrough cartridge can decode these addresses just as an EXT device can.

**Spec base: `0x05F0_0000`** (KSEG1 `0xA5F0_0000`), a 64 KiB window at the top of the range, rather than `0x0500_0000` itself:

- The real 64DD ASIC decodes the low part of the range (C2/sector buffers and ASIC registers around `0x0500_0000–0x0500_05xx`), and SummerCart64's 64DD-emulation mode answers those same low addresses. Sitting high keeps NetPak64 electrically and logically clear of both, preserving the option of coexisting with DD hardware or DD-emulating flashcarts.
- Detection is unambiguous: on a stock console the probe address is open bus. The probe sequence is hardened anyway — read `MAGIC`, sanity-check `VERSION`, then write/read-back a pattern in `CMD_DATA` before declaring the device present.
- Domain 2 bus timing is **software-owned**: unlike Dom1 (seeded from the ROM header at boot), `PI_BSD_DOM2_{LAT,PWD,PGS,RLS}` are programmed by the driver. The spec pins the values; `netpak_init()` writes them. That makes bus-timing behavior a spec constant rather than a header convention — a real win for eventual hardware parity.

Consequence, accepted: NetPak64 and 64DD emulation are mutually exclusive within an ares session (the device attaches where the DD would). Irrelevant for the homebrew target.

### 5.2 Register map (all 32-bit, big-endian, base `0x05F0_0000` + offset)

| Offset | Name | Access | Description |
|---|---|---|---|
| `0x0000` | `MAGIC` | RO | `0x4E503634` ("NP64") — probe/detect |
| `0x0004` | `VERSION` | RO | spec major/minor |
| `0x0008` | `CAPS` | RO | capability bits: transports, max peers, buffer sizes |
| `0x000C` | `STATUS` | RO | `LINK_UP · SESSION · RX_AVAIL · TX_READY · CMD_BUSY · ERROR` |
| `0x0010` | `IRQ_STATUS` | R/W1C | `RX_READY · TX_DONE · EVENT · LINK_CHANGE` |
| `0x0014` | `IRQ_MASK` | RW | enables matching bits; any set+unmasked bit asserts cart IRQ |
| `0x0018` | `CMD` | RW | opcode; write triggers execution |
| `0x001C` | `CMD_STATUS` | RO | idle / busy / ok / errno |
| `0x0020` | `ARG0–ARG3` | RW | 4 × u32 command arguments |
| `0x0030` | `RES0–RES3` | RO | 4 × u32 command results |
| `0x0040` | `TX_LEN` / `TX_DST` | RW | payload length; dest node (`0xFF` = broadcast) + channel |
| `0x0048` | `TX_DOORBELL` | WO | commit the TX buffer |
| `0x0050` | `RX_LEN` / `RX_SRC` | RO | head-of-ring packet length; source node + channel |
| `0x0058` | `RX_CONSUME` | WO | pop ring head, expose next |
| `0x005C` | `RX_COUNT` | RO | packets queued device-side |
| `0x0800` | `CMD_DATA` | RW | 512 B scratch for variable-length command params (room codes, peer lists) and probe write/read-back |
| `0x1000` | `TX_BUF` | RW | 2 KiB — one in-flight packet, MTU 1024 B payload |
| `0x2000` | `RX_WIN` | RO | window onto the ring-head packet (≤ 2 KiB) |

Design intent: **single-packet TX, exposed-head RX** keeps the address decode trivial (kept deliberately MCU/CPLD-friendly even though hardware comes later) while ring depth, retransmission, and relay bookkeeping live on the device side. All buffers are 8-byte aligned and lengths padded to 8 so libdragon PI DMA and cache writeback/invalidate are painless; `io_read`/`io_write` cover the register path and `dma_read`/`dma_write` the buffers.

### 5.3 Command set (mailbox)

`NOP · SET_IDENTITY · SESSION_CREATE · SESSION_JOIN(code) · SESSION_LEAVE · LIST_PEERS · PING(node) · GET_TIME · GET_STATS`. The relay address and transport credentials are **device configuration, not game API** — configured in ares settings (or the adapter's own setup flow on hardware). Games only speak in sessions and peers, which keeps ROMs portable across environments.

### 5.4 Interrupts and timing

RX-ready, TX-done, and link/peer events assert the **cartridge interrupt line** — the same line the 64DD uses, so it exists in ares' N64 model and on both physical connectors. `libnetpak` registers a cart-IRQ handler where the libdragon tree provides one (`register_CART_handler` / `set_CART_interrupt` in current branches) and falls back to `netpak_poll()` pumping otherwise. Bus timing is fully spec-defined via the Dom2 timing registers per §5.1; the ares device honors whatever Dom2 timing the PI model has been programmed with, so software timing behavior transfers to hardware.

## 6. ares integration

- **Attachment model:** a peripheral node in the system tree, occupying the 64DD's attach point (mutually exclusive with a DD node), toggleable in the UI and ideally auto-attachable via the homebrew ROM-header mechanism ares already uses to configure controller paks for homebrew titles. Gated behind or alongside Homebrew Mode, following the ISViewer precedent.
- **Code shape:** new `ares/n64/netpak/` device implementing PI read/write/DMA handlers over `netpak-core`; the diff into ares proper stays small (bus hookup, node registration, savestate serialize) to survive rebases across ares' fast release cadence (v148 as of May 2026).
- **Threading:** the emulation thread never blocks on the network. Doorbell writes push into a lock-free SPSC queue consumed by the net-client thread; inbound packets land in the device ring and set `IRQ_STATUS` via an atomic the scheduler folds into the cart IRQ line on the next slice.
- **Savestates / run-ahead:** register file and rings serialize into savestates; the relay session lives outside them. On loadstate the device bumps an **epoch counter** (exposed to games via `netpak_epoch()`) and flushes rings so netcode can detect the discontinuity and resync. Run-ahead and rewind are documented as unsupported while the device is attached — same operational caveat ares users already accept for 64DD.
- **Config:** P1 via CLI/env (`--netpak relay=…,room=…,name=…`, essential for running two instances on one box); a settings pane later. Mainline fork first; HailToDodongo's N64-only dev fork is a candidate second home; an upstream RFC to ares is a stretch goal once the spec stabilizes.

## 7. libdragon extension library (`libnetpak`)

```c
bool     netpak_detect(void);
int      netpak_init(const netpak_config_t *cfg);   // identity, irq|poll, Dom2 timing setup
int      netpak_session_create(char code_out[8]);
int      netpak_session_join(const char *code);
int      netpak_session_leave(void);
int      netpak_peers(netpak_peer_t *out, int max);  // ids, names, RTT
int      netpak_send(uint8_t dst, uint8_t ch,
                     const void *buf, int len, unsigned flags);
int      netpak_recv(netpak_pkt_t *pkt);             // non-blocking, -EAGAIN
void     netpak_poll(void);                          // pump in poll mode
uint32_t netpak_epoch(void);                         // bumps on loadstate/reconnect
int      netpak_time(netpak_clock_t *clk);           // relay-derived shared clock + RTT stats
```

- **Channels:** `ch0` unreliable-unordered (the default; UDP semantics end-to-end, matching the wire). `ch1` reliable-ordered, implemented **in the library** with a small sliding window — the relay stays dumb, and the same code path works over the physical adapter.
- **Memory discipline:** static pools, no allocation in IRQ paths, RX drained by the cart-IRQ handler into an RDRAM ring the game consumes at its own cadence.
- **Netcode building blocks, not netcode:** RTT estimation and a relay-anchored shared clock give games what they need for lockstep or snapshot interpolation without the library imposing a model.
- **Examples shipped:** `probe` (detect + version), `echo` (latency/soak test with on-screen histogram), `chat` (4-node lobby), `netpong` (2P playable reference with simple prediction).
- Tracks libdragon **preview/unstable** branch (your current setup), with trunk compatibility checked at each phase gate.

## 8. Relay service (`np64-relay`) — UDP from day one

One UDP socket per client, two logical planes over it, no TCP anywhere in the hot path.

```
Frame  { u8 ver; u8 type; u8 flags; u8 channel;
         u32 token; u16 seq; u16 ack;
         u8 src; u8 dst; u16 len; u8 payload[len]; }   // 16 B header
Types:  HELLO, WELCOME, CREATE, JOIN, JOINED, PEER_JOIN, PEER_LEAVE,
        DATA, PING, PONG, TIME, LEAVE, KICK, KEEPALIVE, ERROR
```

- **Handshake & identity:** client sends `HELLO{proto ver, identity, room op}`; relay answers `WELCOME{token, session, node_id}`. Every subsequent frame carries the 32-bit token (spoof resistance + connection identity on a connectionless transport). The relay re-pins the client's source address on any valid-token frame, so NAT rebinds mid-session heal transparently.
- **Control plane (reliable):** `CREATE/JOIN/LEAVE/PEERS/TIME/PING` frames use `seq`/`ack` with retransmit (RTO ≈ 250 ms, doubling, small window) — the NetLib/GameNetworkingSockets-style pattern, implemented once in `netpak-core` and shared by emulator and future firmware.
- **Data plane (fire-and-forget):** `DATA` on `ch0` is forwarded as-is with no relay-side buffering or acks; `ch1` reliability is end-to-end in `libnetpak`. Unicast + broadcast within the room.
- **Liveness:** `KEEPALIVE` every ~10 s holds NAT bindings; relay GCs a node after ~30 s of silence and emits `PEER_LEAVE`.
- **MTU discipline:** frames capped ≈ 1200 B on the wire to dodge fragmentation, comfortably wrapping the device's 1024 B payload MTU.
- **Semantics:** rooms with 6-char join codes, ≤ 8 nodes (games will realistically use 2–4), presence events, per-token rate limiting, idle-room GC.
- **Security posture:** v1 is plaintext UDP with random tokens + room codes — appropriate for the threat model. DTLS (or a Noise-based layer) is the v2 hardening path; a TCP/TLS fallback for UDP-hostile networks is a stretch item, not a launch requirement.
- **Footprint:** worst case, 8 nodes × 30 Hz × 256 B fanned out to 7 peers ≈ 0.4 MB/s per fully saturated room. A $5 VPS hosts the community.
- **Implementation:** Rust + tokio (single static binary, no GC jitter, easy Docker deploy); Prometheus metrics, structured logs, `np64-relay --dev` for the local loop.
- Protocol is deliberately console-agnostic — if the GameCube homebrew exploration grows legs, a GC/BBA client could join the same relay ecosystem.

## 9. Path to real hardware (deferred, not foreclosed)

Spec development is emulator-first, but every choice above stays hardware-plausible: trivial decode at a fixed Dom2 sub-window, software-programmed Dom2 timings, single-packet TX / exposed-head RX, and a `netpak-core` state machine + UDP client written to be liftable into MCU firmware. When the adapter project reaches bring-up, the hardware-side checklist is: drive the cart-IRQ line from the chosen connector, meet the spec's Dom2 timing values, and implement the register file — then an emulator player and a console player join the same room. If hardware constraints force register-map changes, `VERSION`/`CAPS` negotiation lets old ROMs and new devices coexist.

## 10. Phases

**P0 — Spec freeze & recon (≈1 wk).** Register map v0.9 and wire protocol v0.9 written as a standalone `netpak-spec.md`; read ares' PI/DD/cartridge code paths to confirm the attachment plan. (The address-collision survey is now trivial: the Dom2 sub-window only needs a note on DD/SC64-DD low-range avoidance, already handled by the base choice.) *Exit: spec doc reviewed.*

**P1 — Loopback vertical slice (≈2–3 wks).** ares fork with the device attached and a loopback backend (no relay); `libnetpak` probe + echo ROM runs green. *Exit: echo RTT measured in-emulator, IRQ and poll paths both work, savestate round-trips.*

**P2 — UDP relay MVP (≈2–3 wks).** Relay with handshake/token, reliable control plane, rooms; two ares instances (e.g. balthazar + melchior over Tailscale, then a public VPS) run the chat demo. *Exit: cross-machine session with latency histogram captured; NAT-rebind and packet-loss soak pass.*

**P3 — Library & DX hardening (≈2–3 wks).** Reliable `ch1`, time sync, peer events, epoch semantics, docs, `netpong`. *Exit: an outside homebrew dev can go from README to networked ROM in under an hour.*

**P4 — Stretch.** DTLS; TCP/TLS fallback; ares settings UI and upstream RFC; CI (netpak-core unit tests + headless protocol integration tests); port to a second emulator; homebrew ROM-header auto-attach registration; firmware bring-up when the adapter project is ready.

## 11. Risks & mitigations

| Risk | Mitigation |
|---|---|
| ares has no plugin API → fork maintenance burden | Keep the in-tree diff minimal; all logic in out-of-tree `netpak-core`; track releases; pursue upstream once stable |
| Run-ahead/rewind semantics vs. live network | Document as mutually exclusive (existing 64DD precedent); epoch counter for loadstate resync |
| Emulator-first spec needs a hardware-driven rev later | `VERSION`+`CAPS` negotiation from day one; decode and semantics kept MCU/CPLD-trivial anyway |
| UDP blocked on some networks (corp/hotel Wi-Fi) | Stretch TCP/TLS fallback; not a v1 gate for the homebrew audience |
| Control-plane packet loss | Ack'd control channel with retransmit; idempotent room ops |
| NAT rebinding mid-session | Token-based identity with source re-pinning |
| Relay abuse/spam | Tokens, join codes, rate limits, frame caps, room GC; no open lobby listing in v1 |

## 12. Assumptions made (flag anything wrong)

Homebrew-only target via libdragon (no commercial-ROM retrofit). ares mainline fork, built for macOS/Linux/Windows. Sessions of ≤ 8 nodes, ≤ 1 KiB payloads at 10–60 Hz. Relay self-hosted by you, room-code trust model, players on UDP-permissive home networks. Star topology latency acceptable for v1. Hardware follows later against the frozen spec, with version negotiation absorbing any drift. libdragon preview branch is the daily driver. NetPak64 and 64DD are never needed simultaneously.

## 13. Open questions

Target player count / tick rate for your first game (sizes rings and buffers). Hosting (your VPS vs. cloud region) and whether a public community relay is a goal. Master-server/lobby browser (à la NetLib) or join codes only. Project name. License (ISC to match ares / permissive to match libdragon?).
