# 07 — Dynamic Views (Sequence Diagrams)

Six sequences cover the flows where the architecture earns its keep — each crosses at least two ownership domains (game / emulation thread / net thread / relay), which is exactly where designs usually break. Read alongside docs 03–06 for the component definitions.

## 7.1 Probe & init — [`seq-probe-init.puml`](diagrams/seq-probe-init.puml)

Why three probe steps: on a stock console the NetPak window is **open bus** (primer §6.2), and open-bus reads can alias recently driven bus values — a lone `MAGIC` match is not proof of presence. `MAGIC` → `VERSION` sanity → `CMD_DATA` write/read-back is cheap and conclusive. Init then does something unusual for a driver: it **programs the bus itself** (`PI_BSD_DOM2_*` to spec-pinned values), possible only because Dom2 timing is software-owned — the property that made Dom2/Addr1 the right decode home (proposal §5.1). Finally the driver picks IRQ mode (cart-line handler + `IRQ_MASK`) or poll mode (`netpak_poll()` pumping).

## 7.2 Session join — [`seq-session-join.puml`](diagrams/seq-session-join.puml)

Shows the full mailbox lifecycle: `CMD_DATA`+`CMD` write → `CMD_STATUS=busy` → control frame out the SPSC → reliable delivery to the relay (RTO ≈ 250 ms doubling; idempotent handlers make retransmits safe) → `JOINED` ingested **at the next tick** → `CMD_STATUS=ok`, `RES0=node_id`, `EVENT` IRQ. Note the two timescales: the net exchange happens in real time; the *result* becomes architecturally visible only on an emulated-time tick boundary. The one-time `HELLO/WELCOME` handshake (token issuance) belongs to `NetClient` startup, not to any particular command.

## 7.3 End-to-end datagram — [`seq-data-path.puml`](diagrams/seq-data-path.puml)

The signature flow. Points worth attention:

- **Sender:** cache writeback → one PI DMA into `TX_BUF` → descriptor regs → doorbell. The doorbell enqueues to the outbound SPSC **immediately** — TX does not wait for a tick; only inbound visibility quantizes (integration doc §6). Worst-case locally added latency ≈ one tick (0.5 ms) — noise against internet RTT.
- **Relay:** token check, budget check, room lookup, forward. Nothing else — no buffering, no acks on `ch0`.
- **Receiver:** tick ingests into the RX ring → `RX_READY` → cart IRQ → the handler does the minimum (W1C ack, one DMA from `RX_WIN` to the RDRAM ring, `RX_CONSUME`) → the *game* pops via `netpak_recv()` at its own frame cadence. The RDRAM ring is what decouples interrupt timing from game timing.

## 7.4 Savestate & epoch — [`seq-savestate-epoch.puml`](diagrams/seq-savestate-epoch.puml)

The design's answer to "emulator features vs. live network." Device state serializes; the relay session deliberately does not. On load: restore → `epoch++` → `requestFlush()` → recompute IRQ. The game notices `netpak_epoch()` changed and runs an **application-level** resync with its peers over `ch1` — converting an emulator-only anomaly into an ordinary netcode event (late-join/resync) the game must handle anyway. Run-ahead is excluded entirely (tick suppression + no epoch bump during its save/load storms — doc 04 §2).

## 7.5 NAT rebind & liveness — [`seq-nat-rebind.puml`](diagrams/seq-nat-rebind.puml)

Consumer NATs drop or remap idle UDP bindings; some remap even active ones. Two mechanisms make sessions survive: keepalives (~10 s) keep bindings warm, and **token re-pinning** — the relay updates a session's source address on *any* valid-token frame — heals remaps with zero game-visible effect (the same idea as QUIC connection IDs, [RFC 9000 §5.1](https://www.rfc-editor.org/rfc/rfc9000#section-5.1)). The failure path is also shown: ~30 s of silence → GC → `PEER_LEAVE` fanout, surfaced to games as a peer event.

## 7.6 ch1 reliable channel — [`seq-ch1-reliable.puml`](diagrams/seq-ch1-reliable.puml)

Demonstrates loss, gap buffering, RTO retransmit, cumulative acks, and window slide — all inside `libnetpak`, with the relay blind to it. The diagram exists mainly to make the layering vivid: the relay forwards a retransmitted frame exactly like any other. That end-to-end placement is what lets the identical ROM code run over a physical adapter, and it echoes the classic end-to-end argument (Saltzer, Reed & Clark, 1984) as practiced by modern UDP game stacks ([GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets), [Gaffer On Games — Reliable Ordered Messages](https://gafferongames.com/post/reliable_ordered_messages/)).

## Invariants every sequence preserves

1. Architecturally visible state mutates **only** on the emulation thread (register access or tick).
2. Inbound data becomes visible **only** at tick boundaries (determinism under loopback).
3. The net thread touches **only** SPSC queues + atomic status.
4. The relay does **no** work before token validation, and no reliability work for `ch0`/`ch1` data at all.
5. Nothing the ROM observes distinguishes the virtual device from a spec-conformant physical one.
