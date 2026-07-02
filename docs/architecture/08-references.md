# 08 — References

Consolidated bibliography for the architecture set. Inline citations throughout docs 00–07 point here.

## N64 platform documentation

- **N64brew Wiki** — the actively maintained community hardware reference: [Main page](https://n64brew.dev/wiki/Main_Page) · [Peripheral Interface](https://n64brew.dev/wiki/Peripheral_Interface) (PI bus, domains, timing registers, DMA) · [Memory map](https://n64brew.dev/wiki/Memory_map) (KSEG segments, PI address ranges) · [MIPS Interface](https://n64brew.dev/wiki/MIPS_Interface) (interrupt architecture) · [64DD](https://n64brew.dev/wiki/64DD) · [ISViewer](https://n64brew.dev/wiki/ISViewer)
- **NEC VR4300/VR4305/VR4310 User's Manual** — CPU ISA, caches (16 KB I / 8 KB D, 16 B lines, write-back, no snooping), CP0/CAUSE interrupt bits. Widely mirrored; the definitive CPU reference.
- **64dd.org** — [64DD preservation project](https://64dd.org): hardware, Randnet service history, disk format.

## Homebrew ecosystem

- **libdragon** — [libdragon.dev](https://libdragon.dev) · [GitHub](https://github.com/DragonMinded/libdragon). Open-source N64 SDK; `libnetpak` targets its *preview* branch. DMA/cache helper semantics (8-byte alignment for `dma_read`/`dma_write`) and cart-IRQ handler registration come from here.
- **N64brew community** — [n64brew.dev](https://n64brew.dev); Discord + game jams; the target audience for NetPak64.
- **N64-NetLib** (buu342) — [GitHub](https://github.com/buu342/N64-NetLib). Closest prior art: flashcart-USB homebrew networking with a PC bridge and Java servers. Validates demand and the ack'd-UDP design; NetPak64 removes the tether.
- **UNFLoader** (buu342) — [GitHub](https://github.com/buu342/N64-UNFLoader). The standard USB flashcart loader/debug tool; context for the USB-tether status quo.
- **SummerCart64** — [GitHub](https://github.com/Polprzewodnikowy/SummerCart64). Open flashcart whose 64DD-emulation mode answers low Dom2/Addr1 addresses — the coexistence constraint that pushed NetPak64's window to `0x05F0_0000`.
- **64drive** — [retroactive.be](https://64drive.retroactive.be/). Dev flashcart with documented USB protocol; prior art for host↔console links.

## Emulators

- **ares** — [ares-emu.net](https://ares-emu.net) · [GitHub](https://github.com/ares-emulator/ares). Integration target; Node tree, cooperative scheduler, event queue, Homebrew Mode/ISViewer precedent. ISC-licensed.
- **gopher64** — [GitHub](https://github.com/gopher64/gopher64). Rust N64 emulator with input-lockstep netplay; FFI candidate for a second `netpak-core` port.
- **simple64** — [GitHub](https://github.com/simple64/simple64). Input-lockstep netplay reference (the contrasting model to device-level networking).
- **cen64** — [GitHub](https://github.com/n64dev/cen64). Cycle-accurate C emulator; candidate second port.
- **HailToDodongo's N64 dev fork of ares** — see [github.com/HailToDodongo](https://github.com/HailToDodongo) (also author of Tiny3D). Demonstrates that a developer-oriented ares fork is maintainable; candidate second home for the device.

## Networking design

- **Gaffer On Games** (Glenn Fiedler) — [Game networking series](https://gafferongames.com/categories/game-networking/): [UDP vs. TCP](https://gafferongames.com/post/udp_vs_tcp/) · [Packet fragmentation](https://gafferongames.com/post/packet_fragmentation_and_reassembly/) · [Reliable ordered messages](https://gafferongames.com/post/reliable_ordered_messages/). The canonical rationale for the ch0/ch1 split, the 1200 B MTU cap, and drop-oldest queues.
- **GameNetworkingSockets** (Valve) — [GitHub](https://github.com/ValveSoftware/GameNetworkingSockets). Industrial-scale reliable-over-UDP layering; the pattern our control plane and `ch1` follow.
- **RFC 768** — [UDP](https://www.rfc-editor.org/rfc/rfc768).
- **RFC 9000 §5.1** — [QUIC connection IDs](https://www.rfc-editor.org/rfc/rfc9000#section-5.1): connection identity decoupled from addresses — the idea behind token re-pinning.
- **RFC 5905** — [NTPv4](https://www.rfc-editor.org/rfc/rfc5905): conceptual basis for the relay-anchored shared clock.
- **RFC 9147** — [DTLS 1.3](https://www.rfc-editor.org/rfc/rfc9147); **Noise Protocol Framework** — [noiseprotocol.org](https://noiseprotocol.org). The two v2 hardening candidates.
- **Saltzer, Reed & Clark, "End-to-End Arguments in System Design" (1984)** — [MIT copy](https://web.mit.edu/Saltzer/www/publications/endtoend/endtoend.pdf). Why `ch1` reliability lives in the library, not the relay.

## Implementation stack

- **tokio** — [tokio.rs](https://tokio.rs). Relay async runtime.
- **Prometheus** — [prometheus.io](https://prometheus.io). Relay metrics.
- **PlantUML** — [plantuml.com](https://plantuml.com); **C4-PlantUML** — [GitHub](https://github.com/plantuml-stdlib/C4-PlantUML).
- **C4 model** (Simon Brown) — [c4model.com](https://c4model.com). The documentation structure of this set.
