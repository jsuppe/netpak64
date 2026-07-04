# NetPak64

A memory-mapped **network device for the Nintendo 64**, defined by a versioned
hardware spec and implemented emulator-first. Homebrew games see a NIC on the
PI bus — not input-lockstep netplay — and write their own netcode against a
libdragon driver library. Because the contract is a register-level spec, the
same ROM is designed to later run against physical hardware.

**Status (2026-07-02):** P0 ✅ · P1 (loopback vertical slice) ✅ · P2 (UDP
relay) ▶ — two emulated N64s exchange packets through the relay on localhost.
See [`ROADMAP.md`](ROADMAP.md) for the live plan.

## This repository is the project hub

Spec, design docs, architecture set, and dev orchestration. The
implementations live in sibling repos:

| Repo | What | Status |
|---|---|---|
| `netpak64/` (this) | Spec, proposal, architecture docs, ares glue staging | — |
| `../netpak-core/` | Standalone C++ device library (register file, FSM, rings, NetClient) — the reference implementation, submoduled into ares | Host tests green (115k checks) |
| `../libnetpak/` | libdragon C driver + example ROMs (probe, echo, netchat). **Start here as a game developer** — its README is the integration guide | Working ch0 data path, IRQ+poll |
| `../np64-relay/` | Rust/tokio UDP relay: rooms, presence, forwarding | Dev-loop functional |
| ares fork | Branch `netpak` on ares v148: ~400-line glue + `thirdparty/netpak-core` | probe/echo/netchat all green |

## Document map

- [`netpak-spec.md`](netpak-spec.md) — **the normative register map + wire
  protocol** (v0.9). Everything implements this.
- [`netpak64-proposal.md`](netpak64-proposal.md) — product rationale, goals,
  phases, risks (v0.2).
- [`netpak64-ares-integration.md`](netpak64-ares-integration.md) — emulator
  integration design: threading, IRQ, savestates (v0.1).
- [`netpak64-hardware-integration.md`](netpak64-hardware-integration.md) — P4
  physical-adapter design study: gateware/MCU partition, bus interface, BOMs,
  silicon-validation checklist (v0.1, design-only — nothing built yet).
- [`netpak64-security-design.md`](netpak64-security-design.md) — P4 encrypted
  transport design study: DTLS terminates in `NetClient`, never touches the
  N64 CPU; PSK-based, no PKI (v0.1, design-only — nothing implemented yet).
- [`docs/hardware-research-notes.md`](docs/hardware-research-notes.md) — raw
  research + analysis behind the hardware decisions: connector pinouts, joybus
  channels, controller-injection and retail-mirroring assessments, 64DD power,
  form factor.
- [`docs/architecture/`](docs/architecture/) — C4 L1–L3 views, N64 platform
  primer, sequence diagrams, bibliography (browsable site in `docs/web/`).
- [`docs/recon-ares-v148.md`](docs/recon-ares-v148.md) — the R1–R10 recon
  answers with file/line citations.
- [`ROADMAP.md`](ROADMAP.md) — phase status, build environment, next steps.
- [`glue-staging/`](glue-staging/) — the ares in-tree glue + apply script +
  headless test harnesses, staged here until the fork has a remote.

## Quick start (developers)

**Write a game:** read [`../libnetpak/README.md`](../libnetpak/README.md) —
requirements, API walkthrough, epoch/resync pattern, troubleshooting.

**Run the two-instance dev loop** (relay + two headless ares in one room):
see `glue-staging/run_two.sh` and the build-environment section of
`ROADMAP.md`.

**Hack on the device itself:** `../netpak-core/` builds and tests on any host
with CMake + C++17 — no emulator needed.

## License

ISC across the project (matching ares). Spec and docs: same.
