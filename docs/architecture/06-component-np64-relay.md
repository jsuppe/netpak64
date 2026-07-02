# 06 — np64-relay Components (C4 Level 3)

Diagram source: [`diagrams/c4-component-relay.puml`](diagrams/c4-component-relay.puml)

The relay is a deliberately **dumb** star-topology forwarder: rooms, presence, a shared clock, and per-frame token validation — nothing else. Game reliability (`ch1`) is end-to-end in `libnetpak`; the relay never buffers or acks data. Rust + [tokio](https://tokio.rs) for a single static binary with no GC jitter; [Prometheus](https://prometheus.io) metrics and structured logs; `np64-relay --dev` for the localhost loop.

## 1. Wire protocol (spec §8, frozen in P0)

```
Frame  { u8 ver; u8 type; u8 flags; u8 channel;
         u32 token; u16 seq; u16 ack;
         u8 src; u8 dst; u16 len; u8 payload[len]; }   // 16 B header
Types:  HELLO, WELCOME, CREATE, JOIN, JOINED, PEER_JOIN, PEER_LEAVE,
        DATA, PING, PONG, TIME, LEAVE, KICK, KEEPALIVE, ERROR
```

One UDP socket per client; **two logical planes over it**:

- **Control plane (reliable):** `CREATE/JOIN/LEAVE/PEERS/TIME/PING` use `seq`/`ack` with retransmit (RTO ≈ 250 ms doubling, small window). Handlers are **idempotent** — a retransmitted JOIN that already succeeded returns the same JOINED, which is what makes retransmission safe without exactly-once machinery. The pattern is [N64-NetLib](https://github.com/buu342/N64-NetLib)'s and, at industrial scale, [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)'; NetPak64 implements it once in `netpak-core` and shares it with future firmware.
- **Data plane (fire-and-forget):** `DATA` on `ch0` is forwarded as-is — no buffering, no acks. Unicast by `dst` node id; `0xFF` broadcasts to the room. Frames capped ≈ **1200 B** on the wire to dodge IP fragmentation (comfortably wrapping the device's 1024 B payload MTU; see [Gaffer On Games on fragmentation](https://gafferongames.com/post/packet_fragmentation_and_reassembly/) and [RFC 768](https://www.rfc-editor.org/rfc/rfc768)).

## 2. Identity on a connectionless transport

The 32-bit `token` (issued in `WELCOME`, carried on every frame) does two jobs:

1. **Spoof resistance** — frames without a valid token are dropped before any work (and until a token is proven, responses never exceed request size, avoiding UDP amplification).
2. **Connection identity across address changes** — the relay **re-pins** a client's source `addr:port` on *any* valid-token frame. Consumer NATs rebind UDP mappings mid-session routinely; with re-pinning, the session heals transparently without the client even noticing. This is the same insight behind QUIC's connection IDs ([RFC 9000 §5.1](https://www.rfc-editor.org/rfc/rfc9000#section-5.1)), applied at hobby scale.

Liveness: `KEEPALIVE` every ~10 s holds NAT bindings; ~30 s of silence GCs the node and emits `PEER_LEAVE` to the room; idle rooms are reaped.

## 3. Rooms and the clock

Rooms carry 6-char join codes, ≤ 8 nodes (games realistically use 2–4 — proposal §12), presence events, and per-token rate limits. The **clock service** answers `TIME` with relay-anchored timestamps and services `PING/PONG` RTT sampling — the raw material `libnetpak` shapes into `netpak_time()`'s shared clock, conceptually a drastically simplified NTP exchange ([RFC 5905](https://www.rfc-editor.org/rfc/rfc5905)) with the relay as the single time source (defensible because all room members already trust the relay for forwarding).

## 4. Capacity & security posture

Worst case per saturated room: 8 nodes × 30 Hz × 256 B × 7-peer fanout ≈ **0.4 MB/s** — one small VPS hosts a community; a Docker deploy plus Prometheus scrape is the whole ops story.

v1 is **plaintext UDP + random tokens + unguessable room codes** — right-sized for "friends playing homebrew," not for on-path adversaries. Hardening path (v2): DTLS ([RFC 9147](https://www.rfc-editor.org/rfc/rfc9147)) or a [Noise](https://noiseprotocol.org)-based layer; a TCP/TLS fallback for UDP-hostile networks is a stretch item, not a launch gate (proposal §8, §11). Abuse controls that *are* v1: token validation before work, per-token rate limiting, frame caps, room GC, and no public lobby listing.

## 5. Console-agnosticism

Nothing in the protocol is N64-specific — node ids, rooms, channels, and datagrams are platform-neutral by design, so a future GameCube/BBA homebrew client (an exploration mentioned in proposal §8) could join the same relay ecosystem without protocol changes.
