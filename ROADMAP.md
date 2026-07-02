# NetPak64 — Roadmap & Project Plan

**Status: pre-P0.** Nothing has been built yet; the proposal and ares-integration design are frozen enough to start recon. This document tracks repo structure, phase status, and open decisions across the polyrepo. Source of truth for the *technical* spec remains `netpak64-proposal.md` and `netpak64-ares-integration.md` — this file tracks *plan and structure*, not design.

## Documentation status

- ✅ **Architecture documentation set** (`docs/architecture/`, 2026-07-02): N64 platform primer, C4 L1–L3 + deployment views, six sequence diagrams (PlantUML/C4-PlantUML sources in `diagrams/`, all syntax-validated), consolidated bibliography. Expands — does not supersede — the proposal/integration docs; on conflict those win, and the discrepancy gets logged here.
- ✅ **`netpak-spec.md` v0.9, TBDs resolved** (2026-07-02): register map, command set (with opcode numbers), interrupt model, error codes, `CMD_DATA` substructure formats (peer entries, join-code alphabet, identity strings), and wire protocol (with frame-type numbers) extracted and fully pinned. One deliberate exception: the §2.1 Dom2 bus timing constants are marked **provisional** (maximally conservative placeholder values) pending P1 measurement against ares' real PI model — everything else is a final, arbitrary-but-consistent design choice, not something expected to change. Still needs the explicit P0 "spec doc reviewed" sign-off before bumping to v1.0 (see §9 of the spec).
- Re-sync obligations: `docs/architecture/04-component-ares-glue.md` §5 must be updated with confirmed ares symbols when the P0 recon spike lands; §2.1 Dom2 timing values in `netpak-spec.md` should be updated once measured in P1 (this alone does not require a v1.0→v1.1 bump per the spec's own versioning policy §9); architecture doc register/protocol values were written to match this spec's earlier draft and should be spot-checked against the now-concrete bit layouts/opcodes.

## Repository structure (decided 2026-07-02)

Polyrepo, not a monorepo. `netpak-core` has to be independently submodulable into ares *and* future firmware *and* other emulators, so it cannot live inside any one downstream — least of all the ares fork, which per the integration doc's constraint 3 must stay thin and mechanically rebasable across ares' fast release cadence.

```
netpak64/          ← this repo: project hub — spec, design docs, dev orchestration
netpak-core/        ← standalone C++: register file, cmd FSM, rings, netclient,
                       loopback+udp backends, host unit tests, conformance harness
                       (submoduled into ares and, later, firmware)
ares/ (fork)        ← thin glue only: ares/n64/netpak/*, thirdparty/netpak-core submodule
libnetpak/           ← C/libdragon driver + example ROMs (probe, echo, chat, netpong)
np64-relay/          ← Rust + tokio relay, single static binary
```

Submodule direction: **netpak-core is the submodule; ares is the superproject** (never the reverse).

- The human-readable `netpak-spec.md` lives in **this hub repo** — small, stable, referenced by every other repo, shouldn't require cloning ares or the C++ core to read.
- The **executable conformance harness** (headless harness driving `DeviceCore` with scripted PI accesses, per integration doc §11) lives in `netpak-core` next to the reference implementation it verifies — that's the drift alarm between prose spec and behavior.
- This hub stays **plain docs + dev scripts for now**, not a submodule umbrella pinning the other four repos. Revisit only if one-clone onboarding pain shows up in practice (it directly serves the P3 exit criterion below). Don't pay the submodule tax speculatively.
- Within the ares fork, nothing beyond the four glue files + the `netpak-core` submodule + the CMake option + node registration + serialization hookup. Any ring buffer, FSM, or protocol logic proposed for the fork should move to `netpak-core` instead.

## Phases (from netpak64-proposal.md §10)

Status legend: ☐ not started · ▶ in progress · ✅ done

- **▶ P0 — Spec freeze & recon** (≈1 wk)
  ✅ Extract `netpak-spec.md` as a standalone doc in this hub.
  ✅ Resolve spec TBDs (bit layouts, opcode/type numbering, error codes, buffer formats). One item (Dom2 timing constants) intentionally left provisional rather than blocking — see Documentation status above.
  ✅ Resolve the ares recon checklist R1–R10 against ares **v148** (`0aafd8578921`) — all ten confirmed, no design changes needed. Full report: `docs/recon-ares-v148.md`; summary table in `docs/architecture/04-component-ares-glue.md` §5.
  ☐ Explicit spec review sign-off (bumps `netpak-spec.md` to v1.0 per its own §9). **This is now the only open P0 item.**
  *Exit: spec doc reviewed; R1–R10 answered with file/symbol citations.* ← second half met 2026-07-02

- **✅ P1 — Loopback vertical slice** — **COMPLETE 2026-07-02** (in one day, vs. the 2–3 wk estimate). All three exit criteria met: echo RTT measured in-emulator (~1 ms, = 2 device ticks, byte-identical across runs), IRQ and poll paths both green, savestate round-trips with game-visible epoch resync. Caveat: `netpak-core`/`libnetpak`/ares-fork exist as local repos + staged trees only (no git remotes yet, per the local-only decision); publishing them is a P2-entry chore.
  ✅ `netpak-core` skeleton created (`../netpak-core/`, sibling repo, ISC) and **host unit tests green on balthazar** (115k checks, 0 fail): register file with the half/word + low-half-side-effect semantics, W1C IRQ model + level IRQ, command-FSM dispatch (NOP/SET_IDENTITY/GET_STATS sync; session/net ops → BUSY; EBUSY re-entry), RX ring drop-oldest, TX doorbell (silent-ignore-when-busy), SPSC (incl. threaded SPSC soak), wire-frame codec (exact big-endian layout), and an end-to-end loopback round-trip. `spec.hpp` pins every `netpak-spec.md` value in code — the executable-review half of the spec sign-off.
  ✅ ares fork glue (decode + enable + env config) → **`probe` ROM detects the device in ares** (2026-07-02). ares v148 branch `netpak` on melchior; glue at `ares/n64/netpak/` + `thirdparty/netpak-core/` submodule-staged; in-tree diff = exactly the 5 files recon predicted. Verified end-to-end headless: `NP64_ENABLE=1` → `NP64_PROBE: DETECTED version=0000.0000` + NOP mailbox returns OK; disabled → `NP64_PROBE: NOT_PRESENT` with zero `[NetPak64]` activity (zero-diff-when-disabled shown at runtime). Glue sources + `apply_glue_edits.py` + `run_headless.sh` live in `glue-staging/`.
  ✅ Tick + rings + IRQ → **`echo` ROM green on loopback, both modes** (2026-07-02): `NP64_ECHO_POLL: PASS n=32 rtt_us 945/994/1234`, `NP64_ECHO_IRQ: PASS n=32 rtt_us 950/1011/1482`, 0 dropped, payload integrity verified. RTT ≈ 1 ms emulated = the predicted 2 device ticks; **RTT stats byte-identical across runs** (loopback determinism observed in practice). libnetpak grew the data path: `netpak_init` (Dom2 timing + IRQ|poll), `netpak_send/recv/poll`, cart-IRQ handler draining into an RDRAM ring. Key gotcha discovered: libdragon's `dma_read`/`dma_write` mangle PI addresses into the cart-ROM range (documented historical quirk) — devices outside `0x1000_0000+` MUST use `dma_{read,write}_raw_async` + `dma_wait`. This belongs in the libnetpak docs and the spec's driver notes.
  ✅ Serialization + savestate round-trip (2026-07-02). Three layers: (1) host test — full `DeviceCore::serialize` round-trip with mid-session state (queued RX, committed TX, busy FSM), PI-sweep state equality + behavioral continuation (115k checks green); (2) live in-emulator — `NP64_TEST_SAVESTATE=1` glue hook restores a snapshot mid-soak: ROM observes `EPOCH_CHANGE 1→2` via the new EPOCH register, exactly 1 in-flight packet lost (by design), soak continues clean for 700+ packets after; (3) real savestates — `s(netpak)` wired into `System::serialize`, guarded on `enabled()` so disabled sessions keep upstream-compatible layout. **Spec addition:** `EPOCH` RO register at `0x0060` (implementation surfaced that §7's epoch had no register; additive/minor-compatible). netpak-core gained the host-agnostic `np64::Serializer` seam; libnetpak gained `netpak_epoch()`. Soft-reset semantics host-tested via `reset(bool)`; GUI-driven reset/savestate exercise deferred until interactive testing.
  ☐ Swap loopback for `backend_udp` + threaded NetClient against `np64-relay --dev` → **P2 begins**. ← **NEXT**
  Not yet in netpak-core: threaded/UDP `NetClient` + `backend_udp`, command completion via `tick()`, serialization.

  **Headless test harness (P1 how-to):** ares' desktop-ui gates emulation on window focus and blocks on a startup modal, so it will not advance under bare Xvfb. Working recipe (in `glue-staging/run_headless.sh`): Xvfb + **openbox** WM + **xdotool** to focus the window and press Return to dismiss the modal, plus `--no-file-prompt --setting Input/Defocus=Allow`. ISViewer `printf` reaches stdout; the `probe` ROM prints `NP64_PROBE:` marker lines a scripted run asserts on. Builder image `ares-builder` on melchior includes openbox/xdotool/mesa GL.
  *Exit: echo RTT measured in-emulator; IRQ and poll paths both work; savestate round-trips.*

- **▶ P2 — UDP relay MVP** (≈2–3 wks)
  ✅ **Two-instance chat demo on localhost** (2026-07-02): `np64-relay --dev` (Rust/tokio, `../np64-relay/`, ~250 lines) + two headless ares instances in room `DEVROM` exchanged ~1,450 packets each way — `NP64_TWO_INSTANCE: PASS` via `glue-staging/run_two.sh`. Implemented: HELLO/WELCOME handshake with random token, addr re-pin on any valid-token frame, rooms with join codes (auto-create on empty code; spec §5.1 alphabet), presence fanout (PEER_JOIN/LEAVE), keepalive + 30 s GC, DATA forwarding (unicast/broadcast) with **server-side src stamping** (spoof prevention). netpak-core gained the threaded `NetClient` (POSIX sockets; SPSC + atomic status only — the threading rule holds), DeviceCore consumes WELCOME/PEER frames + `setLink()`; glue: `NP64_RELAY`/`NP64_ROOM`/`NP64_NAME` config, NetClient survives soft reset / reconstructs on hard power; libnetpak: `netpak_status()`; new `netchat` ROM.
  **Next steps, in order:**
  ☐ **(a) Check in all repos** — `git init` + initial commits: hub, netpak-core, libnetpak, np64-relay locally; ares `netpak` branch commit on melchior. Remotes/publishing when ready. *(in progress 2026-07-02)*
  ✅ **(b) Control-plane reliability + soaks** (2026-07-02). `CtlPlane` in netpak-core: pure seq/ack retransmit logic (RTO 250 ms doubling capped 4 s, window 4, forever-ops for HELLO) — deterministic host soak: 3 seeds × 500 ops through a simulated 40%-loss/duplicating/reordering link, all complete (116.6k checks green). Relay: WELCOME/PONG/TIME ack the request seq, **idempotent HELLO replay** (same addr → same WELCOME), PING/TIME clock service (µs since relay start), `--dev-loss P` bidirectional loss injection (no root/netem needed). NetClient: HELLO via CtlPlane, periodic PING → `rttUs()` (Karn's algo = later refinement), `NP64_TEST_REBIND` socket-rebind hook. **System soaks green:** 20% loss each way → session up + ~1,000 pkts/way delivered (68% ≈ the 64% two-leg theoretical); 9 NAT rebinds/instance → ~1,420 pkts/way (≈ lossless baseline), token re-pin healed every one. Device-side GET_TIME plumbing lands with (c).
  ☐ **(c) Mailbox session commands complete via tick** — SESSION_CREATE/JOIN/LEAVE/LIST_PEERS/PING currently park at BUSY; wire FSM → control frames → responses → `CMD_STATUS=OK`/`RES*`/`CMD_DATA` per spec §5, so ROMs can create/join rooms dynamically (config-driven join then becomes the dev-loop convenience, not the only path). Includes the peer table for `LIST_PEERS` (§5.1 PeerEntry format).
  ☐ (d) Per-token rate limiting; Prometheus metrics endpoint.
  ☐ (e) Cross-machine session (melchior ↔ balthazar over Tailscale — blocked on balthazar returning) with latency histogram.
  *Exit: cross-machine session with latency histogram captured; NAT-rebind and packet-loss soak pass.*

- **☐ P3 — Library & DX hardening** (≈2–3 wks)
  Reliable `ch1`, time sync, peer events, epoch semantics, docs, `netpong`.
  *Exit: an outside homebrew dev can go from README to networked ROM in under an hour.* (This is the bar that would justify promoting the hub to a submodule umbrella.)

- **☐ P4 — Stretch**
  DTLS; TCP/TLS fallback; ares settings UI and upstream RFC; CI (netpak-core unit tests + headless protocol integration tests); port to a second emulator; homebrew ROM-header auto-attach registration; firmware bring-up when the adapter project is ready.

## Build environment (decided + verified 2026-07-02)

**Active build host: melchior** (`ssh melchior@melchior`) — Ubuntu 24.04 x86_64, 10 cores, docker (no passwordless sudo → ares builds run **inside docker**, image `ares-builder`, built from ares' own CI dep list: `libsdl2-dev libgtk-3-dev libao-dev libopenal-dev qt6-base-dev` + toolchain). ares checkout: `~/dev/ares`, tag v148, branch `netpak`, with `thirdparty/netpak-core/` staged and glue at `ares/n64/netpak/`. Build: `docker run --rm -v ~/dev/ares:/src -v ares-ccache:/ccache -e CCACHE_DIR=/ccache -e HOME=/tmp -u $(id -u):$(id -g) ares-builder bash -c "cmake --preset ubuntu-ci && cd build && ninja"`. netpak-core host tests: `~/dev/netpak-core` (g++ 13.3, green). Glue sources are authored in this hub at `glue-staging/` (incl. `apply_glue_edits.py`, the idempotent in-tree edit script) until the fork gets its own remote. Disk caution: 97% full (~27 G free).

**balthazar** (`ssh jsuppe@balthazar`, Apple M5 Mac) — the original build host, **currently unavailable**; its ares v148 native build and libdragon container (`sweet_colden`, mounts `~/mygame`) remain intact for when it returns. The local Windows machine is for docs/spec/orchestration only.

- **N64 ROMs:** existing Docker container `sweet_colden` (`ghcr.io/dragonminded/libdragon:latest`, mips64-elf-gcc 14.4, tiny3d), mounting `~/mygame → /libdragon`. Smoke-tested: `make` produces `hello.z64`. ✅
- **ares:** native macOS build at `~/dev/ares`, upstream clone **pinned to v148** (`0aafd8578921`) — the exact version the integration doc targets, and the recon target for R1–R10. Build: `cmake --preset macos` && `cmake --build build_macos --parallel 10` → `build_macos/desktop-ui/Debug/ares.app` (arm64, verified runnable). ✅ One-time setup that was needed: `xcodebuild -downloadComponent MetalToolchain` (ares' ruby AV layer compiles Metal shaders; newer Xcode ships that toolchain as a separate download).
- SSH PATH note: non-interactive commands need `export PATH=/opt/homebrew/bin:/usr/local/bin:$PATH`.
- balthazar ↔ melchior over Tailscale is the planned two-machine pair for P2 relay testing.

## Implementation order within P0–P1 (from netpak64-ares-integration.md §14)

1. ✅ Recon spike filling integration doc §13 (`docs/recon-ares-v148.md`).
2. ✅ `netpak-core` skeleton + host unit tests green (registers, loopback backend, SPSC) — done 2026-07-02, `../netpak-core/`.
3. ✅ Glue: decode + enable setting + env config → `probe` ROM detects the device in ares — done 2026-07-02 (see P1 above; `glue-staging/`).
4. ✅ Tick event + rings + IRQ line → `echo` ROM green against loopback, IRQ and poll modes — done 2026-07-02.
5. ✅ Serialization + reset semantics → savestate round-trip test green — done 2026-07-02.
6. Swap loopback for `backend_udp` against `np64-relay --dev` → P2 begins.

## Open decisions (from netpak64-proposal.md §13, unresolved)

- Target player count / tick rate for the first game (sizes rings and buffers).
- Hosting for the relay (own VPS vs. cloud region) and whether a public community relay is a goal.
- Master-server/lobby browser (à la N64-NetLib) vs. join codes only.
- Project name (working title only so far).
- ~~License~~ — **provisionally resolved: ISC** (matches ares, the primary consumer). Applied to `netpak-core` 2026-07-02. Revisit before any public release if a more permissive license is preferred; trivial to change while repos are private/small.

## Non-goals reminder (v1, from netpak64-proposal.md §2)

Not doing in v1: commercial-ROM retrofit, TCP/IP stack on the N64, P2P/NAT traversal (relay-only), rollback netcode in the library, DTLS/encryption, web/browser clients. Don't scope-creep phase work to include these; they're explicitly deferred, not forgotten (see proposal §9 for the hardware deferral story and §11 for risk mitigations that keep the door open).
