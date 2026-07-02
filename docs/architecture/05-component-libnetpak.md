# 05 — libnetpak Components (C4 Level 3)

Diagram source: [`diagrams/c4-component-libnetpak.puml`](diagrams/c4-component-libnetpak.puml)

`libnetpak` is the ROM-side driver: a C library in the [libdragon](https://libdragon.dev) ecosystem (tracking the *preview* branch, trunk checked at phase gates) that turns the raw register device into a small, non-blocking, game-loop-shaped API. Everything in it must run on real hardware unmodified — it may use nothing but PI accesses, the cart IRQ, and libdragon primitives.

## 1. API surface (proposal §7)

```c
bool     netpak_detect(void);
int      netpak_init(const netpak_config_t *cfg);   // identity, irq|poll, Dom2 timing setup
int      netpak_session_create(char code_out[8]);
int      netpak_session_join(const char *code);
int      netpak_session_leave(void);
int      netpak_peers(netpak_peer_t *out, int max);  // ids, names, RTT
int      netpak_send(uint8_t dst, uint8_t ch, const void *buf, int len, unsigned flags);
int      netpak_recv(netpak_pkt_t *pkt);             // non-blocking, -EAGAIN
void     netpak_poll(void);                          // pump in poll mode
uint32_t netpak_epoch(void);                         // bumps on loadstate/reconnect
int      netpak_time(netpak_clock_t *clk);           // relay-derived shared clock + RTT stats
```

Design stance: **netcode building blocks, not netcode.** RTT estimation and a relay-anchored shared clock give a game what it needs for lockstep or snapshot interpolation (see [Gaffer On Games](https://gafferongames.com/categories/game-networking/) for the canonical treatment of these models) without the library imposing one. Contrast with input-lockstep netplay, where the *emulator* owns the model and the game can't choose.

## 2. Hardware-facing discipline

- **Probe** must be safe on a stock console where `0x05F0_0000` is open bus: read `MAGIC` (`0x4E503634`, "NP64"), sanity-check `VERSION`, then write/read-back a pattern in `CMD_DATA` before declaring the device present. Three steps because open-bus reads can alias recently driven bus values — a single magic read is not proof.
- **Init programs the bus itself**: `PI_BSD_DOM2_{LAT,PWD,PGS,RLS}` written to spec-pinned values (primer §6.3 — Dom2 timing is software-owned, which is precisely why NetPak64 chose it).
- **Register vs. buffer paths**: registers via `io_read`/`io_write` (uncached KSEG1 single accesses); `CMD_DATA`/`TX_BUF`/`RX_WIN` via `dma_read`/`dma_write` with explicit cache writeback/invalidate (primer §3). All spec buffers are 8-byte aligned with lengths padded to 8 so these calls are unconditionally legal.
- **Memory discipline**: static pools only, **no allocation in IRQ paths**. The cart-IRQ handler does the minimum — ack `IRQ_STATUS` (W1C), DMA the head packet from `RX_WIN` into the RDRAM ring, `RX_CONSUME` — and returns; the game consumes the RDRAM ring at its own cadence via `netpak_recv()`. Poll mode (`netpak_poll()`) does the same work synchronously for trees/setups without a cart-line handler.

## 3. Channels

- **`ch0` — unreliable, unordered (default).** UDP semantics end-to-end; a datagram either arrives once or not at all. Matches the wire exactly; zero library bookkeeping.
- **`ch1` — reliable, ordered.** A small sliding window (seq/ack/retransmit) implemented **in the library**, not the relay. Two reasons: the relay stays a dumb forwarder (cheap, stateless-ish, no per-room replay buffers), and — decisively — the identical code path works over a physical adapter, because the reliability logic lives in ROM code that ships with the game. This end-to-end placement follows the classic end-to-end argument and the same layering GameNetworkingSockets uses over UDP.

## 4. Epoch: making savestates a game-level event

Emulator users *will* savestate mid-session. The device can't hide that from a network that kept moving — so it doesn't try. `netpak_epoch()` returns a counter the device bumps on loadstate, reconnect, or soft reset; a game checks it once per frame and, on change, runs its own resync (re-request state snapshot, rejoin lockstep, etc.). This converts an emulator-only anomaly into an ordinary netcode event the game already has to handle for late joins.

## 5. Shipped examples (P1–P3 deliverables)

| ROM | Purpose |
|---|---|
| `probe` | Detect + version report — first-light test and integration canary |
| `echo` | Latency/soak test with on-screen histogram — the P1 exit criterion runs this against loopback |
| `chat` | 4-node lobby — exercises sessions, peers, presence events (P2 exit) |
| `netpong` | 2-player playable reference with simple prediction — the copy-me example for outside devs (P3 exit: README → networked ROM in under an hour) |
