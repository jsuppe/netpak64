# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Current state

This repo is **design-stage only** (pre-P0, see `ROADMAP.md`). There is no source tree, build system, or tests yet — just versioned design documents, a roadmap, and a session helper. The actual implementation (ares fork, `netpak-core`, `libnetpak`, `np64-relay`) does not live here — this repo is the project **hub**: spec, design docs, and (later) dev orchestration, not a monorepo for the code.

- `netpak64-proposal.md` — the top-level proposal (v0.2). Product philosophy, goals/non-goals, phases (P0–P4), risks, open questions. Register/protocol *values* now live in `netpak-spec.md`; this doc remains the source for *rationale*.
- `netpak64-ares-integration.md` — the ares emulator integration design (v0.1, companion to the proposal). Code layout, bus decode, threading/state ownership, IRQ delivery, savestate/reset semantics, config, build, tests, and a recon checklist (R1–R10) of ares symbols to confirm before coding.
- `netpak-spec.md` — **the normative device register map and UDP wire protocol** (v0.9 draft, P0 deliverable). Extracted from the two docs above. Explicitly marks unpinned values (bit layouts, opcode/frame-type numbering, timing constants) as **TBD** rather than inventing them — check here before assuming a spec value is frozen, and resolve TBDs here rather than in code comments.
- `ROADMAP.md` — repo structure decision, phase status, implementation order, and tracked open decisions. Check this before starting phase work or creating new repos.
- `docs/architecture/` — the expanded architecture set: N64 platform primer, C4 L1–L3 views with PlantUML sources (`diagrams/*.puml`, C4-PlantUML stdlib includes), deployment views, six sequence diagrams, and a consolidated bibliography. See its `README.md` for the doc map and rendering instructions. **Precedence rule: where this set and the proposal/integration docs disagree, the proposal/integration docs win** until re-synced (note discrepancies in `ROADMAP.md`).
- `session.ps1` — one-liner that launches `claude --dangerously-skip-permissions`.

When asked to work on NetPak64, the primary job at this stage is refining these specs and, as implementation begins, scaffolding the four deliverables (in their own repos, per `ROADMAP.md`) to match them. Treat the register map, wire protocol, and the three architectural constraints (below) as the source of truth — code is an implementation of the spec, not the other way around.

## Repository structure

Polyrepo — see `ROADMAP.md` for the full rationale. Summary: `netpak-core` (standalone C++) is a submodule inside the `ares` fork (never the reverse), because it must also be reusable for firmware and other emulators. `libnetpak` and `np64-relay` are separate repos with their own release cadences. This hub stays plain docs + dev scripts unless onboarding pain justifies promoting it to a submodule umbrella (P3 exit criterion).

## What NetPak64 is

A memory-mapped **network device for the Nintendo 64**, defined by a single versioned hardware spec and implemented emulator-first. The key framing: **the game sees a NIC, not netplay.** It is device-level networking (games write their own netcode against a driver library), not input-lockstep netplay like gopher64/simple64. Because the contract is a register-level spec on the PI bus rather than an emulator API, the same ROM and driver can later run against physical hardware.

## The four deliverables, one contract

1. **The spec** (`netpak-spec.md`, to be extracted/frozen in P0) — register map, PI timing, IRQ semantics, command set, wire protocol. The keystone; everything else implements it.
2. **`netpak-core`** — standalone, dependency-light C++ library: register file, command FSM, ring buffers, UDP relay client, loopback backend. Unit-testable with no emulator. Enters the ares fork as a `thirdparty/` git submodule. Also the reference implementation for future adapter firmware.
3. **ares integration** — thin glue (`ares/n64/netpak/`, ~300–500 lines) over `netpak-core`: bus decode, node registration, a recurring tick event, serialization. Mirrors the existing `dd` (64DD) component's shape.
4. **`libnetpak`** — libdragon-side C driver and game-facing API (session join/create, datagram send/recv, peers, RTT/clock, `netpak_epoch()`).
5. **`np64-relay`** — self-hostable UDP relay (Rust + tokio): rooms with join codes, star-topology forwarding, presence, shared clock. `np64-relay --dev` for the local loop.

## Architecture facts that span multiple docs

**Device bus mapping.** PI Domain 2 / Address 1 (the 64DD's EXT space). Base `0x05F0_0000` (KSEG1 `0xA5F0_0000`), a 64 KiB window sitting *high* to stay clear of the real 64DD ASIC and SC64's DD-emulation (which answer the low range near `0x0500_0000`). **Consequence: NetPak64 and 64DD emulation are mutually exclusive** — the device attaches where the DD would.

**Register map lives in `netpak64-proposal.md` §5.2.** `MAGIC` = `0x4E503634` ("NP64"). Single-packet TX (`TX_BUF` at `0x1000`, MTU 1024 B), exposed-head RX (`RX_WIN` at `0x2000`); ring depth/retransmit live device-side. This "trivial decode" is deliberate so an MCU/CPLD front end could honor it later. All buffers 8-byte aligned, lengths padded to 8 for libdragon PI DMA + cache discipline.

**Access width.** Physical PI bus is 16-bit (AD16); register file is 16-bit-granular with 32-bit registers assembled from two halves, big-endian, high half first. **Side effects (doorbells, W1C, `CMD` trigger, `RX_CONSUME`) fire on the low half completing a word** — so a 32-bit store or DMA burst behaves identically to two half stores, and the semantics transfer to real hardware.

**Interrupts.** RX-ready / TX-done / events assert the **cartridge interrupt line** (CAUSE IP3) — the same line the 64DD uses. Level-triggered: `irqLine = |(IRQ_STATUS & IRQ_MASK)`.

**Relay protocol lives in `netpak64-proposal.md` §8.** 16-byte UDP frame header, 32-bit token per frame (spoof resistance + identity on a connectionless transport; relay re-pins source addr on any valid-token frame so NAT rebinds heal). Two planes over one socket: reliable control plane (`seq`/`ack` + retransmit) and fire-and-forget data plane. `ch0` unreliable-unordered (default, matches wire); `ch1` reliable-ordered implemented **in `libnetpak`**, not the relay — the relay stays dumb so the same code path works over physical hardware.

## Three constraints that drive every ares-integration choice

These are non-negotiable (from `netpak64-ares-integration.md` §1) — check any implementation change against them:

1. **Zero behavioral diff when disabled.** With NetPak off, ares must be bit-identical to upstream (decode falls through to existing dd/open-bus paths). This keeps the fork rebasable and an upstream RFC credible.
2. **All architecturally visible device state is owned by the emulation thread.** The net thread touches only two SPSC queues and one atomic `LinkStatus` word — never registers, rings, FSM, or anything a savestate serializes. This makes savestates race-free and loopback fully deterministic.
3. **Glue is thin; logic is out-of-tree in `netpak-core`.** Keeps the in-ares diff mechanical to rebase across ares' fast release cadence.

**Determinism corollary:** inbound packets become architecturally visible only at tick-event boundaries (~0.5 ms emulated time). With the loopback backend, same ROM ⇒ same trace, which is what makes in-emulator CI regression tests possible.

**Savestates:** register file + rings + FSM + epoch serialize; the relay session lives *outside* the savestate. On loadstate the device bumps an **epoch counter** (`netpak_epoch()`) and flushes rings so game netcode can detect the discontinuity and resync. Run-ahead and rewind are unsupported while attached (same stance users accept for 64DD).

## Working on the specs

- The two docs are **versioned and cross-referenced** (proposal v0.2 ↔ integration v0.1). Keep the header version lines and cross-references consistent when editing.
- Before writing any ares code, the recon checklist (`netpak64-ares-integration.md` §13, R1–R10) must be resolved against a real ares checkout — it lists exact symbols/files to confirm (shared PI decode point, cart-IRQ helper, queue-event registration, access width, run-ahead flag, `Node::Setting::Boolean` precedent, serialization registration, CMake mechanics).
- Implementation order is specified: proposal phases P0–P4, and integration §14 (recon → `netpak-core` skeleton + host tests → glue/decode → tick+rings+IRQ → serialization → swap loopback for UDP).
- Assumptions and open questions are tracked explicitly (proposal §12–13). Licensing (ISC/permissive to match ares/libdragon) and project name are still open.
