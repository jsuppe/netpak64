# 00 — N64 Platform Primer

Everything the rest of this documentation assumes about the Nintendo 64, written for engineers who have never touched the console. Readers who know the platform can skim §6–§8, which cover the parts NetPak64 leans on hardest. Authoritative community references: the [N64brew wiki](https://n64brew.dev/wiki/Main_Page) (the modern, actively corrected source for N64 hardware documentation) and the NEC VR4300 User's Manual for CPU details.

## 1. The machine in one paragraph

The Nintendo 64 (1996) pairs a **NEC VR4300** CPU — a 64-bit MIPS III core clocked at 93.75 MHz, almost always used with 32-bit pointers — with the **RCP** (Reality Coprocessor, 62.5 MHz), an SGI-designed chip containing the vector signal processor (**RSP**), the rasterizer (**RDP**), and *all* of the system's I/O interfaces. Main memory is **4 MB of Rambus RDRAM** (8 MB with the Expansion Pak), unified between CPU, RSP, and RDP. There is no operating system at runtime: the game ROM is the whole software stack. Everything external — the cartridge, the 64DD disk drive — hangs off one shared bus, the **Peripheral Interface (PI)**, which is where NetPak64 lives.

## 2. CPU address space: KSEG segments

The VR4300 uses classic MIPS address segmentation ([N64brew: Memory map](https://n64brew.dev/wiki/Memory_map)). Two segments matter daily:

| Segment | Virtual range | Maps to | Cached? |
|---|---|---|---|
| KSEG0 | `0x8000_0000–0x9FFF_FFFF` | physical `0x0000_0000+` | **Yes** (through CPU caches) |
| KSEG1 | `0xA000_0000–0xBFFF_FFFF` | physical `0x0000_0000+` | **No** (uncached, direct) |

So physical address `0x05F0_0000` (the NetPak64 base) is touched by software at KSEG1 `0xA5F0_0000` for uncached register access. Memory-mapped I/O is *always* accessed via KSEG1 — a cached read of a device register would hand you a stale cache line, not the device. The TLB-mapped user segment exists but homebrew rarely uses it.

## 3. Caches and the coherence problem

The VR4300 has a 16 KB instruction cache and an **8 KB write-back data cache with 16-byte lines** — and, critically, **no bus snooping**. DMA engines write straight to RDRAM behind the cache's back. Coherence is entirely software's job:

- Before DMA **out of** a buffer you wrote with the CPU: write back the dirty lines (`data_cache_hit_writeback` in libdragon).
- Before CPU-reading a buffer DMA **filled**: invalidate those lines (`data_cache_hit_invalidate`).

Getting this wrong yields the classic symptom of "the packet is fine sometimes." This is why the NetPak64 spec pins **all device buffers 8-byte aligned and lengths padded to 8** (libdragon's `dma_read`/`dma_write` require 8-byte-aligned RDRAM addresses), and why `libnetpak` (doc 05) keeps its RDRAM rings cache-line disciplined.

## 4. The RCP and its interfaces

The RCP contains the RSP and RDP plus six memory-mapped interface blocks the CPU programs:

| Block | Role |
|---|---|
| **MI** (MIPS Interface) | Interrupt aggregation for RCP-internal sources; version/init registers |
| **VI** (Video) | Framebuffer scan-out |
| **AI** (Audio) | Audio DMA to DAC |
| **PI** (Peripheral) | **The cartridge/64DD bus + its DMA engine — NetPak64's home** |
| **SI** (Serial) | Controllers/PIF (joybus) |
| **RI** (RDRAM) | RDRAM configuration |

## 5. Interrupts

The VR4300 takes interrupts through CAUSE-register bits ([N64brew: MIPS Interface](https://n64brew.dev/wiki/MIPS_Interface)):

| CAUSE bit | Source |
|---|---|
| IP2 | RCP aggregate — MI collects SP/SI/AI/VI/PI/DP into one line |
| **IP3** | **Cartridge INT line** — a physical pin on the cart edge and the 64DD connector |
| IP4 | Pre-NMI (console Reset button pressed; ~0.5 s warning before NMI) |
| IP7 | CPU internal COUNT/COMPARE timer |

Note the asymmetry: PI **DMA-complete** interrupts arrive via MI on IP2, but a **peripheral wanting attention on its own initiative** uses the dedicated cart line on IP3. The 64DD is the canonical IP3 user; **NetPak64 asserts the same line** (level-triggered, `IRQ_STATUS & IRQ_MASK`), which is why it works identically in an emulator that models the DD and, later, on a physical connector. libdragon exposes handler registration for this line (`register_CART_handler` in current branches).

## 6. The Peripheral Interface (PI) — NetPak64's bus

This is the section to internalize. Primary reference: [N64brew: Peripheral Interface](https://n64brew.dev/wiki/Peripheral_Interface).

### 6.1 Physical layer: AD16

The PI bus is a **16-bit multiplexed address/data bus** ("AD16"). A transaction latches a 32-bit address in two phases (ALE_H, ALE_L strobes), then moves 16-bit data words with read/write strobes. Both the **cartridge port** (top) and the **EXT port** (bottom, where the 64DD docks) carry *the same bus* — electrically, a device can't tell which connector it's on. Two consequences NetPak64 exploits:

1. A spec written against PI addresses is a **connector-neutral contract**: a bottom-port adapter and a passthrough cartridge can both implement it.
2. The natural register granularity is **16 bits**. NetPak64's 32-bit registers are architecturally two big-endian halves, with side effects defined to fire when the **low half** completes — making a 32-bit CPU store, a DMA burst, and two 16-bit stores behave identically (integration doc §5.2).

### 6.2 Address decode: domains and ranges

The PI decodes its address space into regions, each assigned one of **two timing domains** (Dom1, Dom2):

| Physical range | Domain/Addr | Conventional occupant |
|---|---|---|
| `0x0500_0000–0x05FF_FFFF` | **Dom2 / Addr1** | **64DD control registers — NetPak64 window at `0x05F0_0000`** |
| `0x0600_0000–0x07FF_FFFF` | Dom1 / Addr1 | 64DD IPL ROM |
| `0x0800_0000–0x0FFF_FFFF` | Dom2 / Addr2 | Cartridge SRAM / FlashRAM saves |
| `0x1000_0000–0x1FBF_FFFF` | Dom1 / Addr2 | Cartridge ROM |

NetPak64 sits at the **top 64 KiB of Dom2/Addr1** (`0x05F0_0000–0x05F0_FFFF`). The real 64DD ASIC (and [SummerCart64](https://github.com/Polprzewodnikowy/SummerCart64)'s DD-emulation mode) answers the *low* part of that range (~`0x0500_0000–0x0500_05xx`), so sitting high avoids both. On a stock console with nothing attached, reads there return **open bus** — which is what makes the probe sequence (read `MAGIC`, check `VERSION`, write/read-back scratch) unambiguous.

### 6.3 Bus timing registers

Each domain has four CPU-programmable timing registers — `PI_BSD_DOMn_{LAT, PWD, PGS, RLS}` — controlling, in PI clock cycles: address-to-data **latency**, strobe **pulse width**, **page size** (words per address issue), and bus **release** time. The key asymmetry:

- **Dom1** timings are seeded at boot from bytes in the ROM header (the IPL reads them) — a *convention* owned by whatever ROM is inserted.
- **Dom2** timings are **software-owned**: whatever the driver programs is what the bus does.

NetPak64 deliberately chose Dom2 so the spec can **pin the timing values** and have `netpak_init()` program them — bus behavior becomes a spec constant rather than a header convention, which matters enormously for eventual hardware parity (proposal §5.1). ares models PI transfer durations from these registers, so emulated DMA pacing tracks future hardware for free (integration doc §5.3).

### 6.4 Two access paths: CPU direct vs. PI DMA

Software reaches PI space two ways, and NetPak64 uses both:

1. **Direct (uncached) CPU access** — a KSEG1 load/store; the CPU stalls for the whole bus transaction. Fine for registers (`io_read`/`io_write` in libdragon); ruinous for bulk data.
2. **PI DMA** — the CPU programs `PI_DRAM_ADDR`, `PI_CART_ADDR`, and a length register; the PI engine moves the block between RDRAM and the bus while the CPU runs on. Completion raises the PI interrupt (via MI/IP2). This is the path for `TX_BUF`/`RX_WIN` payloads. Only one PI DMA runs at a time, system-wide — the PI is a shared, contended resource, which is why the device's data structures are designed to need *one* DMA per packet.

## 7. The cartridge, the 64DD, and period networking hardware

- **Cartridge**: ROM plus optional save memory on the PI bus; no DMA mastering of its own — the console always initiates. Modern flashcarts ([SummerCart64](https://github.com/Polprzewodnikowy/SummerCart64), [64drive](https://64drive.retroactive.be/), EverDrive-64) add USB links to a PC, which is how most homebrew debugging works today.
- **64DD** (1999, Japan-only): magnetic disk drive docking under the console on the EXT port; decodes Dom2/Addr1 registers and drives the IP3 cart interrupt — the direct architectural template for NetPak64 ([N64brew: 64DD](https://n64brew.dev/wiki/64DD), [64dd.org](https://64dd.org)). Its **Randnet** service (modem cartridge + dial-up ISP) plus the **SharkWire Online** cartridge and **Morita Shogi 64** (a cartridge with a built-in modem, used for networked shogi play) establish that memory-mapped network peripherals are period-authentic N64 engineering, not an anachronism.
- **ISViewer**: Intelligent Systems' development-cartridge debug channel — a small register window in cart space (near `0x13FF_0000`) that dev-kit tools read text from. Emulators including ares implement it to give homebrew `printf`-style output ([N64brew: ISViewer](https://n64brew.dev/wiki/ISViewer)). It is the in-tree precedent NetPak64's ares integration follows: *a bus-mapped pseudo-device that exists to serve homebrew* — NetPak64 is the bidirectional generalization.

## 8. The modern homebrew stack

- **[libdragon](https://libdragon.dev)** ([GitHub](https://github.com/DragonMinded/libdragon)) — the open-source N64 SDK: GCC/newlib toolchain, boot IPL, C runtime, display/audio/controller drivers, DMA + cache helpers, interrupt registration. NetPak64's `libnetpak` is a libdragon extension library; the project tracks libdragon's *preview* branch. The legal alternative to Nintendo's proprietary SDK, and the reason a from-scratch networked ROM is practical.
- **[N64brew](https://n64brew.dev)** — the community (wiki, Discord, game jams) where the homebrew audience for NetPak64 lives.
- **Emulators**: **[ares](https://ares-emu.net)** ([GitHub](https://github.com/ares-emulator/ares)) — multi-system, accuracy-focused, cycle-aware N64 core, "Homebrew Mode" with ISViewer support; our integration target. **[gopher64](https://github.com/gopher64/gopher64)** (Rust) and **[simple64](https://github.com/simple64/simple64)** ship *input-lockstep netplay* — synchronized controller streams into deterministic instances, which suits retail games but gives homebrew no real network device (see doc 01 for positioning). **[cen64](https://github.com/n64dev/cen64)** (C, cycle-accurate) is a candidate second port for `netpak-core`.
- **[N64-NetLib](https://github.com/buu342/N64-NetLib)** (buu342) — the closest prior art: homebrew networking via flashcart USB, a PC-side bridge app, and Java servers (same author as [UNFLoader](https://github.com/buu342/N64-UNFLoader), the standard USB flashcart loader). It validates demand and UDP-with-reliability design; NetPak64's differentiator is removing the PC tether by making the emulator itself the network endpoint.

## 9. Vocabulary quick reference

| Term | Meaning here |
|---|---|
| PI | Peripheral Interface — the shared cart/64DD bus and its DMA engine |
| Dom2/Addr1 | PI decode region `0x0500_0000–0x05FF_FFFF` with software-programmed timings |
| Cart IRQ / IP3 | The peripheral-initiated interrupt line NetPak64 asserts |
| KSEG1 | Uncached CPU window; all device register access goes through it |
| Open bus | What reads of unmapped PI addresses return (no device driving the bus) |
| Doorbell | A write-only register whose write *is* the command ("this buffer is ready") |
| W1C | Write-1-to-clear semantics (`IRQ_STATUS`) |
| Epoch | Device counter bumped on loadstate/reconnect so game netcode can detect discontinuity |

Full citations: [08 — References](08-references.md).
