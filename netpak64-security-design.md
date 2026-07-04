# NetPak64 × Security — Encrypted Transport & Device Identity Design

**v0.1 · companion to netpak64-proposal.md v0.2 and netpak-spec.md v0.9**
Scope: the P4 stretch item named in ROADMAP.md ("DTLS") and proposal §11 · Status: **design study — nothing implemented yet**

---

## 0. What this answers, what it doesn't

This resolves *"can NetPak64 have encrypted, authenticated transport?"* — yes, and it's cheap because of where it terminates (§1). It does **not** resolve *"can a physical hardware device cryptographically prove its own identity?"* — that's a PKI/provisioning problem, explicitly deferred to §8. Conflating the two would over-scope a bounded task into an open-ended one; keep them separate.

## 1. The key fact: this never touches the N64 CPU

`NetClient` (netpak-core) owns the socket; the N64 CPU only ever exchanges plaintext frames with it across the SPSC queue boundary (integration doc §6). That boundary doesn't move. Concretely:

- **Emulator**: `NetClient` is a `std::thread` on the host machine (Mac/Linux/Windows) — a full CPU, TLS libraries, no constraint.
- **Hardware** (per `netpak64-hardware-integration.md` §3): the network MCU (ESP32 or RP2040+W5500) runs `NetClient`, not the gateware and not the N64 CPU. **ESP32 has hardware AES acceleration** and ships `mbedTLS` in ESP-IDF; RP2040 has none but is fast enough in software at these data rates (§6).
- **The ROM never sees a DTLS record.** `libnetpak`'s `netpak_send`/`netpak_recv` and the wire frame format in `netpak-spec.md` §8 are **completely unchanged** — encryption is a transport wrapper one layer below the existing wire protocol, applied and removed entirely inside `NetClient`.

This is the same reasoning that made "the game sees a NIC, not netplay" work for netcode: push protocol concerns to the layer that isn't resource-constrained. Here it extends one layer further down.

## 2. Threat model: what v1 has, what this adds

Recap (`netpak-spec.md` §8.9): v1 is **plaintext UDP**, authenticated only by a random 32-bit per-connection token + unguessable room codes. Explicitly scoped for "friends playing homebrew together," not an on-path adversary. What's already in v1: per-token rate limiting, frame size caps, room GC, no public room listing, **server-side `src` stamping** (a client cannot forge another player's node id — the relay assigns it).

**What plaintext-UDP-plus-token does NOT protect against:** anyone on-path (shared Wi-Fi, a hostile relay operator's upstream, a malicious relay itself) can read every packet, or inject forged packets for a token they've observed. The token is bearer-only — whoever has it *is* that session.

**What DTLS adds:** confidentiality (on-path reading defeated) and integrity/authenticity (on-path injection/tampering defeated) for the relay link. It does **not** by itself add stronger *identity* than v1 has today — see §4 on why the token model is kept, not replaced.

## 3. Protocol impact: none

DTLS wraps the existing UDP datagrams; the 16-byte wire header and every frame type in `netpak-spec.md` §8 are unchanged — they become the DTLS *application data* payload instead of the raw UDP payload. No opcode, register, or frame-type change. This is a `NetClient`/relay transport swap, not a spec revision.

**MTU check:** DTLS record overhead (header + MAC, ~13–29+ bytes depending on cipher suite) added to the existing 16-byte frame header + up to 1200 B payload (spec's `WIRE_MAX_FRAME`) stays comfortably under a standard 1500 B Ethernet MTU. No fragmentation concern at the current design center.

**DoS consideration:** DTLS 1.2's `HelloVerifyRequest` cookie exchange (RFC 6347 §4.2.1) should be enabled relay-side — it's the standard defense against using a UDP relay as a reflection/amplification vector, and costs nothing for legitimate clients.

## 4. Identity model: keep the token, wrap it in DTLS — don't build a PKI (yet)

Two designs were considered:

- **(rejected for v2) Mutual TLS / per-client certificates.** Requires a certificate authority or manual per-client provisioning — no such infrastructure exists for a self-hosted "spin up `np64-relay --dev`" relay, and inventing one is exactly the open-ended PKI problem §0 says to defer. Appropriate later for hardware device attestation (§8), premature for "encrypt the friends-group relay link."
- **(recommended) DTLS with a pre-shared key (PSK) or a self-signed/pinned relay certificate, session identity unchanged.** The relay operator generates a PSK (or self-signed cert) once; clients are configured with it out-of-band exactly like they're configured with `NP64_RELAY=host:port` today — it's one more environment value, e.g. `NP64_RELAY_PSK=<hex>`. **The existing HELLO/WELCOME token handshake (spec §8.3) runs unchanged *inside* the now-encrypted channel.** DTLS answers "is this link private and untampered," the token still answers "which session is this" — don't conflate the two layers.

This mirrors how SSH host-key fingerprints or a self-hosted Mumble/game server's cert typically work: no public CA, trust is established by the operator distributing a secret or fingerprint through the same out-of-band channel they'd share the relay address through anyway.

## 5. Library choices

| Component | Recommendation | Why |
|---|---|---|
| **`netpak-core` / `NetClient`** (all targets: host thread, ESP32, RP2040) | **mbedTLS** | One library, one codebase, across every `netpak-core` target — host build for the emulator, ESP-IDF (has it built in) and pico-sdk (available as a library) for hardware. Small footprint, DTLS 1.2 supported, is the de facto embedded-TLS standard this project's other choices (dependency-light C++, ISC-friendly) already point toward. |
| **`np64-relay`** (Rust/tokio) | **`webrtc-dtls`** (pure Rust, async, PSK+cert support; used in production by the `webrtc-rs` project) preferred; **`openssl` crate** as fallback if it proves immature | Keeps the relay's "single static-ish binary, no config files" property (README's own stated goal) — a system OpenSSL dependency is a real regression against that. Re-evaluate at implementation time against current crate maturity. |

**DTLS version:** target **DTLS 1.2 (RFC 6347)** for the initial cut — broader, more mature library support in both mbedTLS and the Rust DTLS crates today. Note: `netpak-spec.md` §8.9 currently cites **RFC 9147 (DTLS 1.3)** as the named path; this doc recommends starting at 1.2 for maturity reasons and revisiting 1.3 once library support settles — **flag this as a spec correction to make explicitly**, not silently diverge from what's written.

## 6. Performance: why this is fine on every target, including hardware

The N64 CPU is excluded by construction (§1), so the only performance question is symmetric-crypto throughput on the network MCU, against the project's own design center (spec §8.8): **≤1024 B payloads at 10–30 Hz ≈ tens of KB/s.**

- **ESP32**: hardware AES acceleration; this workload is noise.
- **RP2040** (no hardware crypto): software AES-128-GCM or ChaCha20-Poly1305 via mbedTLS at 133 MHz handles tens of KB/s trivially — this is a mainstream embedded-DTLS workload, not a stretch for this chip.
- **Handshake cost** (asymmetric crypto, the expensive part) happens once per session-join, which the driver notes (`netpak-spec.md` §5.2) already document as tolerant of multi-second wall-clock delay (the relay round-trip already dominates). A slower ECDHE handshake on an RP2040 is invisible against that existing budget.
- **Emulator `NetClient`**: a host CPU thread — not a consideration at all.

## 7. Deployment: opt-in, not a hard cutover

- **Config surface** (mirrors the existing `NP64_RELAY` env var pattern, `libnetpak` README §6): a scheme prefix, e.g. `NP64_RELAY=dtls://host:port` selects the encrypted backend; bare `host:port` keeps today's plaintext path. `NP64_RELAY=loopback` (the deterministic CI/dev-loop backend) is untouched — no reason to encrypt a same-process loopback.
- **Relay** can listen in dual-mode (plaintext port + DTLS port) during a transition period, or DTLS-only if the operator chooses — an `np64-relay` CLI flag, analogous to the existing `--dev-loss`/`--metrics` flags.
- **Dev loop stays plaintext.** `run_two.sh`/`--dev` should not require certificate/PSK setup for local testing — encryption is for real deployments, not the inner dev loop.

## 8. Explicitly deferred: hardware device identity / attestation

A physical NetPak64 adapter cryptographically proving *"I am this specific, genuine unit"* (vs. "I know a PSK") is a materially different, harder problem: it needs a provisioning story (unique key burned in at flash time? signed by a project CA? self-attested and pinned TOFU-style?), and it only matters once real silicon exists (`netpak64-hardware-integration.md` HW-M3+). Do not let this doc's scope creep to solve it — record it here as the named follow-on and stop.

## 9. Scoped task list (if/when this is picked up)

1. **Spec note**: add a §8.9 addendum recording the DTLS 1.2-vs-1.3 decision (§5) — a clarification, not a breaking change.
2. **`netpak-core`**: new `backend_dtls.hpp`/`.cpp` implementing the same socket-backend shape `NetClient::openSocket()`/`pumpOnce()` already isolate (confirmed clean boundary — no `DeviceCore`/`CtlPlane` changes needed); mbedTLS as an optional CMake dependency (`NETPAK_WITH_DTLS`, off by default so host tests need no new dependency).
3. **`np64-relay`**: add a DTLS listener (PSK-mode first — smallest surface), `--dtls-psk`/`--dtls-cert` flags, keep the plaintext listener for dev/back-compat.
4. **`libnetpak`**: no code changes expected (§1) — only the `NP64_RELAY` scheme-prefix docs update.
5. **Soak**: rerun the existing packet-loss/NAT-rebind soaks (P2 harness, `run_two.sh` + `--dev-loss`) against the DTLS path to confirm the reliability layer (`CtlPlane`, `ch1`) is unaffected by the transport swap — it should be, since DTLS sits below it, but the existing soak infrastructure makes this a cheap confirmation rather than an assumption.

## 10. Open questions

- PSK distribution UX for non-technical relay operators (a config file? a join-code-adjacent secret shared alongside the room code?) — bikeshed-able, not architecturally load-bearing.
- Whether to eventually support both PSK and cert modes simultaneously, or pick one and stay simple — lean toward PSK-only for v2 given the self-hosted, no-CA context (§4); revisit only if a real deployment demands certs.
- DTLS 1.3 migration timing (§5) — track library maturity, not urgent.

---

*Companion documents: `netpak-spec.md` (§8.9 names this as the v2 hardening path), `netpak64-proposal.md` (§9/§11 risk register), `netpak64-hardware-integration.md` (where the network MCU that would run this actually lives).*
