# 02 — Containers & Deployment (C4 Level 2)

Diagram sources: [`diagrams/c4-containers.puml`](diagrams/c4-containers.puml), [`diagrams/c4-deployment.puml`](diagrams/c4-deployment.puml)

## 1. Reading the container view

A "container" here (C4 sense: independently runnable/deployable unit or clearly bounded runtime element) is unusual in this project because one of the containers is a **MIPS binary running inside an emulated console inside another container**. The stack, from the inside out:

```
game code ──calls──► libnetpak ──PI loads/stores/DMA──► [emulated bus boundary]
   ──► NetPak glue (ares) ──direct call──► DeviceCore (netpak-core)
   ──SPSC queues──► NetClient thread ──UDP──► np64-relay ──UDP──► peers
```

The **emulated bus boundary** is the load-bearing interface: everything to its left is spec-portable ROM code that would run unmodified on real hardware; everything to its right is host-side implementation detail.

## 2. Containers within one player's machine

| Container | Tech | Responsibility | Key interface |
|---|---|---|---|
| ares desktop-ui | C++ / hiro | Frontend, settings tree, AV | ares `Node` tree |
| ares N64 core | C++ cooperative scheduler | Cycle-aware console model | PI decode, event queue, IP3 line |
| Game ROM + libnetpak | MIPS/libdragon | Game + driver, inside the console | Device registers at `0x05F0_0000` (spec) |
| NetPak glue | C++ (`ares/n64/netpak/`) | Decode, tick event, IRQ, serialization | ≤ ~500 lines; detailed in doc 04 |
| netpak-core `DeviceCore` | C++ library | All architecturally visible device state | `readHalf/writeHalf/tick/serialize`; doc 03 |
| netpak-core `NetClient` | C++, own `std::thread` | Socket I/O, handshake, retransmit, keepalive | Two SPSC queues + atomic `LinkStatus`; doc 03 |

**The one threading rule** (integration doc §6, restated because every container above obeys it): *all architecturally visible device state is owned by the emulation thread.* The net thread's entire world is two single-producer/single-consumer queues and one atomic. There is no lock on the emulation thread's path; savestates are race-free by construction; loopback runs are deterministic. When reviewing any change to these containers, check it against this rule first.

**Why `DeviceCore` and `NetClient` are separate containers of one library:** they have different owners (emulation thread vs. own thread), different lifetimes (`DeviceCore` serializes into savestates; `NetClient` deliberately survives soft reset and dies on hard power), and different futures (`DeviceCore` + FSM lift into adapter firmware; `NetClient` maps onto an MCU's network stack). The split *is* the hardware-portability seam.

## 3. The relay host

`np64-relay` is a single static Rust binary ([tokio](https://tokio.rs) runtime — chosen for no-GC latency jitter and trivial Docker deploys) with a Prometheus metrics endpoint ([prometheus.io](https://prometheus.io)). One UDP socket per client connection; two logical planes over it (reliable control, fire-and-forget data). Detailed in doc 06.

Capacity math that justifies "a $5 VPS covers the community" (proposal §8): worst case per fully saturated room = 8 nodes × 30 Hz × 256 B fanned out to 7 peers ≈ **0.4 MB/s**. Realistic homebrew rooms (2–4 players, 10–30 Hz) are an order of magnitude below that.

## 4. Deployment views

**Dev loop (the configuration that must never regress):** two ares instances + `np64-relay --dev` on one machine, all configured by env vars (`NP64_RELAY`, `NP64_ROOM`, `NP64_NAME`, `NP64_LOG` — precedence: env > `netpak.bml` beside the ROM > defaults). A degenerate form uses `NP64_RELAY=loopback`: the built-in echo backend, no sockets at all, fully deterministic — this is the CI configuration.

**Production:** relay on a public VPS; players behind consumer NAT. The protocol is designed for this topology: keepalives every ~10 s hold NAT bindings, the token re-pins a client's source address transparently mid-session, and the ~1200 B frame cap dodges path-MTU fragmentation (a hard-won lesson of every UDP game protocol; see [Gaffer On Games — Packet Fragmentation](https://gafferongames.com/post/packet_fragmentation_and_reassembly/) and UDP itself, [RFC 768](https://www.rfc-editor.org/rfc/rfc768)).

## 5. Cross-container data formats

Three formats cross container boundaries; keeping them distinct avoids a classic confusion:

1. **Device register/buffer layout** (spec §5.2) — crosses the emulated bus. Big-endian 32-bit registers as two AD16 halves; 1024 B payload MTU.
2. **`Frame`** (netpak-core internal) — crosses the SPSC queues between `DeviceCore` and `NetClient`. In-memory struct, host-endian; *not* wire format.
3. **Wire frame** (spec §8) — crosses UDP. 16 B header `{ver, type, flags, channel, token u32, seq u16, ack u16, src u8, dst u8, len u16}` + payload; explicit serialization in `frame.hpp` / relay codec, never `memcpy` of a struct (endianness + padding portability, and the adapter firmware must produce byte-identical frames).

## 6. What is deliberately *not* a container

- **No PC-side bridge app** — the defining difference from [N64-NetLib](https://github.com/buu342/N64-NetLib); the emulator is the endpoint.
- **No game server** — the relay forwards; game logic stays in the ROMs. (NetLib's Java game servers are the contrasting choice.)
- **No TCP anywhere in the hot path** — reliability where needed is seq/ack over UDP in `netpak-core` (control) and `libnetpak` (`ch1`), the [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)-style layering. A TCP/TLS fallback for UDP-hostile networks is a stretch item (proposal §11).
