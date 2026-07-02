# NetPak64 Device & Wire Protocol Specification

**Version: v0.9 (TBDs resolved — pending P0 review sign-off before v1.0)**
**Status:** P0 deliverable — extracted from `netpak64-proposal.md` v0.2 §5, §8 and `netpak64-ares-integration.md` v0.1 §5. This document is now the **normative source** for register/protocol values; the proposal and integration docs remain the source for *rationale* and *design narrative*. Where this spec and those docs conflict on a *value*, this spec wins and the discrepancy should be logged in `ROADMAP.md`.

**Audience:** implementers of any of the three conformant parties — the ares virtual device (`netpak-core` + glue), the ROM-side driver (`libnetpak`), and (later) physical adapter firmware — plus the relay (`np64-relay`), which speaks §8 but not §1–§6.

**On this revision:** every value that was marked **TBD** in the v0.9 draft has been assigned a concrete value below. Two different confidences apply:

- **Final** (bit layouts, opcode/frame-type numbering, error codes, buffer formats): arbitrary-but-consistent choices. Changing them later is a breaking change (§9), not a correction — there's no "true" value to converge on, just a need for every implementation to agree.
- **Provisional** (the Dom2 bus timing constants, §2.1 only): a deliberately conservative starting point, not a measured value. Expect this one specific set of numbers to be tuned in P1 once real throughput can be measured against ares' PI model (recon item R4). This is the one place where "resolved" does not yet mean "final."

Per `ROADMAP.md`, bumping this document to v1.0 additionally requires the P0 "spec doc reviewed" exit criterion — a review step, not just TBD resolution — so version stays at v0.9 until that happens explicitly.

---

## 1. Conventions

- All multi-byte device register fields are **big-endian**.
- All wire protocol (relay) frame fields are **big-endian** (network byte order), consistent with the device side.
- Device addresses in this document are **physical PI addresses**. Uncached CPU access is via KSEG1 (add `0xA000_0000`): e.g. physical `0x05F0_0000` → CPU address `0xA5F0_0000`. See `docs/architecture/00-n64-platform-primer.md` §2 for segment background if this is unfamiliar.
- "Half" = 16 bits (the PI bus's native AD16 granularity). "Word" = 32 bits, assembled from two halves, **high half first**.
- All device-visible buffers (register windows and DMA buffers alike) are **8-byte aligned**, and all lengths passed to DMA are **padded to a multiple of 8 bytes** — required for libdragon's `dma_read`/`dma_write` and for cache writeback/invalidate correctness (primer §3). This convention also applies to substructures placed in `CMD_DATA` (§5.1).
- Multi-bit sub-fields within a register are written `[hi:lo]`, e.g. `[7:0]` is the low byte.

## 2. Bus mapping

| Property | Value |
|---|---|
| PI domain / address space | Domain 2 / Address 1 (the 64DD's EXT decode range) |
| Decode range occupied by NetPak64 | `0x05F0_0000–0x05F0_FFFF` (64 KiB window) |
| Full Dom2/Addr1 range (context) | `0x0500_0000–0x05FF_FFFF` |
| KSEG1 (uncached CPU) base | `0xA5F0_0000` |
| Bus timing ownership | **Software-owned.** `PI_BSD_DOM2_{LAT,PWD,PGS,RLS}` are programmed by `netpak_init()` to the values in §2.1, not seeded from the ROM header. |
| Mutual exclusion | NetPak64 and 64DD emulation/hardware are mutually exclusive within one session — the device attaches at the DD's position. |

**Rationale for sitting at the top of the range, not the bottom** (`0x0500_0000`): the real 64DD ASIC and SummerCart64's DD-emulation mode answer the low part of Dom2/Addr1 (register/sector-buffer addresses near `0x0500_0000–0x0500_05xx`). The 64 KiB window at `0x05F0_0000` stays electrically and logically clear of both.

**Detection is unambiguous only if probed correctly**: on a stock console, this address range is **open bus**. A single `MAGIC` read is not proof of presence (open-bus reads can alias recently-driven bus values) — see §2.2.

### 2.1 Dom2 timing values — **PROVISIONAL**

| Register | Field width | v0.9 value | Meaning at this value |
|---|---|---|---|
| `PI_BSD_DOM2_LAT` | 8 bits | `0xFF` | Maximum latency (slowest, safest) |
| `PI_BSD_DOM2_PWD` | 8 bits | `0xFF` | Maximum pulse width (slowest, safest) |
| `PI_BSD_DOM2_PGS` | 4 bits | `0x0F` | Maximum page size field value |
| `PI_BSD_DOM2_RLS` | 2 bits | `0x03` | Maximum release field value |

These are the **maximally conservative legal values** for each field — deliberately chosen as a starting point rather than a measured one. Slower-than-necessary Dom2 timing costs throughput but cannot cause a correctness bug; the reverse (timing too aggressive for the PI model or future hardware) can. `netpak_init()` MUST write these four values before any TX/RX activity.

**Action for P1:** once `netpak-core`'s loopback and UDP backends are running against real ares (recon item R4, integration doc §13), measure actual achievable throughput and tune these down if the `echo` ROM's RTT/soak numbers justify it. Record the tuned values here, replacing this table, and remove the PROVISIONAL marker for this subsection only — the rest of this document does not depend on this outcome.

### 2.2 Probe sequence (normative)

Implementations MUST perform all three steps, in order, before declaring the device present:

1. Read `MAGIC` (offset `0x0000`) — expect exactly `0x4E503634` ("NP64" in ASCII).
2. Read `VERSION` (offset `0x0004`) — sanity-check major version is a value the driver understands (see §7).
3. Write a driver-chosen pattern to `CMD_DATA` (offset `0x0800`) and read it back — expect an exact match.

A failure at any step means "device not present"; the driver MUST NOT proceed to `netpak_init()`.

## 3. Register map

All registers are relative to the window base `0x05F0_0000`. Access width is 16-bit (AD16) native; 32-bit registers are two consecutive halves, high half first. Side effects defined in §4 fire when the **low half** of a word-defined register completes. Reads at offsets not listed below, within the 64 KiB window, return `0xFFFF` per half.

| Offset | Name | Width | Access | Description |
|---|---|---|---|---|
| `0x0000` | `MAGIC` | u32 | RO | `0x4E503634` ("NP64") — probe/detect |
| `0x0004` | `VERSION` | u32 | RO | Spec major (high half) / minor (low half) |
| `0x0008` | `CAPS` | u32 | RO | Capability bits — layout in §3.1 |
| `0x000C` | `STATUS` | u32 | RO | Bitfield: `LINK_UP` · `SESSION` · `RX_AVAIL` · `TX_READY` · `CMD_BUSY` · `ERROR` — layout in §3.2 |
| `0x0010` | `IRQ_STATUS` | u32 | R / W1C | Bitfield: `RX_READY` · `TX_DONE` · `EVENT` · `LINK_CHANGE` — layout in §3.3 |
| `0x0014` | `IRQ_MASK` | u32 | RW | Enables matching `IRQ_STATUS` bits (same layout as §3.3); any set-and-unmasked bit asserts the cart IRQ line |
| `0x0018` | `CMD` | u32 | RW | Opcode (see §5); write triggers execution |
| `0x001C` | `CMD_STATUS` | u32 | RO | `idle` / `busy` / `ok` / `errno` — encoding in §3.4 |
| `0x0020`–`0x002C` | `ARG0`–`ARG3` | 4×u32 | RW | Command arguments |
| `0x0030`–`0x003C` | `RES0`–`RES3` | 4×u32 | RO | Command results |
| `0x0040` | `TX_LEN` / `TX_DST` | u32 | RW | High half: payload length (bytes). Low half: dest node id (`0xFF` = broadcast) in high byte, channel in low byte |
| `0x0048` | `TX_DOORBELL` | u32 | WO | Any write commits the current `TX_BUF` contents for transmission (see §4 for busy-write behavior) |
| `0x0050` | `RX_LEN` / `RX_SRC` | u32 | RO | Head-of-ring packet length + source node id + channel (same layout as `TX_LEN`/`TX_DST`) |
| `0x0058` | `RX_CONSUME` | u32 | WO | Any write pops the ring head and exposes the next packet (if any) at `RX_WIN` |
| `0x005C` | `RX_COUNT` | u32 | RO | Packets currently queued device-side (saturates; does not wrap) |
| `0x0060` | `EPOCH` | u32 | RO | Session-discontinuity counter: increments on loadstate, soft reset, and reconnect (§7). `netpak_epoch()` reads this; game netcode polls it once per frame and resyncs at the application level on change. *(Added when implementation surfaced that the epoch was spec'd conceptually in §7 but had no register; additive/minor-compatible per §9.)* |
| `0x0800` | `CMD_DATA` | 512 B | RW | Scratch region for variable-length command parameters (room codes, peer lists) and probe write/read-back — substructure formats in §5.1 |
| `0x1000` | `TX_BUF` | 2 KiB | RW | Single in-flight TX packet buffer. Payload MTU: 1024 B |
| `0x2000` | `RX_WIN` | ≤2 KiB | RO | Window onto the ring-head packet only (exposed-head design — see §3.5) |

### 3.1 CAPS bit layout

| Bits | Name | Meaning |
|---|---|---|
| `[0]` | `CAP_TRANSPORT_UDP` | Always `1` in this spec version; reserved for future non-UDP transports |
| `[1]` | `CAP_TRANSPORT_LOOPBACK` | `1` if this build supports the loopback backend (dev/test builds; informational only, not game-visible behavior) |
| `[3:2]` | Reserved | Must read `0` |
| `[7:4]` | `MAX_PEERS_M1` | Max peers supported minus 1. v0.9 devices report `7` (→ 8 peers), matching the room cap in §8.8 |
| `[11:8]` | `TX_MTU_CLASS` | `0` = 1024 B payload MTU (this version). Values 1–15 reserved for future larger-MTU hardware classes |
| `[15:12]` | `RX_RING_DEPTH_CLASS` | `0` = implementation-default ring depth, not otherwise pinned by this spec version |
| `[31:16]` | Reserved | Must read `0` |

### 3.2 STATUS bit layout

| Bit | Name | Meaning |
|---|---|---|
| `0` | `LINK_UP` | `NetClient` (or firmware equivalent) has an active relay connection or loopback link |
| `1` | `SESSION` | Device currently holds a joined/created session |
| `2` | `RX_AVAIL` | Convenience mirror of `RX_COUNT > 0` |
| `3` | `TX_READY` | TX slot is free; a `TX_DOORBELL` write is currently legal |
| `4` | `CMD_BUSY` | Convenience mirror of `CMD_STATUS.CODE == BUSY` (§3.4) |
| `5` | `ERROR` | Convenience mirror of `CMD_STATUS.CODE == ERROR`; latched until the next `CMD` write |
| `[31:6]` | Reserved | Must read `0` |

`STATUS` is an intentionally redundant, cheap-to-poll aggregate of state that's also derivable from `CMD_STATUS`/`RX_COUNT` — it exists so a driver's hot poll loop can check one register instead of several.

### 3.3 IRQ_STATUS / IRQ_MASK bit layout

Both registers share the same bit positions.

| Bit | Name | Meaning |
|---|---|---|
| `0` | `RX_READY` | A packet is available at `RX_WIN` |
| `1` | `TX_DONE` | The TX slot has become free again |
| `2` | `EVENT` | A mailbox command (§5) has completed |
| `3` | `LINK_CHANGE` | `STATUS.LINK_UP` or `STATUS.SESSION` changed |
| `[31:4]` | Reserved | In `IRQ_STATUS`: W1C writes to these bits are ignored. In `IRQ_MASK`: must be written `0` |

### 3.4 CMD_STATUS encoding

| Bits | Name | Meaning |
|---|---|---|
| `[7:0]` | `CODE` | `0x00`=`IDLE`, `0x01`=`BUSY`, `0x02`=`OK`, `0x03`=`ERROR` |
| `[15:8]` | `ERRNO` | Valid only when `CODE == ERROR` (§3.4.1); reads `0` otherwise |
| `[31:16]` | Reserved | Must read `0` |

#### 3.4.1 Errno values

| Value | Name | Meaning |
|---|---|---|
| `0x01` | `EINVAL_OPCODE` | Unknown or invalid `CMD` opcode |
| `0x02` | `EBUSY` | `CMD` written while the FSM was already `BUSY` (busy re-entry, §4) |
| `0x03` | `ENOTJOINED` | Session-scoped command issued with no active session |
| `0x04` | `EALREADY` | **Reserved in v0.9** — `SESSION_CREATE`/`SESSION_JOIN` while in a session have *move semantics* instead (the relay implicitly leaves the current room first; see §5). Kept for implementations that later opt into strict membership |
| `0x05` | `ENOPEER` | Target node id (e.g. for `PING`) not found in the peer table |
| `0x06` | `ETIMEOUT` | Control-plane operation exhausted its retry budget (relay unreachable) |
| `0x07` | `EBADARG` | Malformed argument (e.g. join code of the wrong length/alphabet) |
| `0x08` | `ERELAY` | Relay returned a wire-protocol `ERROR` frame (§8.2) for this operation |
| `0xFF` | `EUNKNOWN` | Catch-all for conditions not covered above |
| others | Reserved | Must not be produced by a conformant v0.9 implementation |

### 3.5 Why single-packet TX / exposed-head RX

This shape keeps address decode trivial — a fixed window and a handful of registers, deliberately CPLD/MCU-friendly — while ring depth, retransmission bookkeeping, and relay-session state live device-side, where a software (or eventually firmware) implementation can afford them. The consequence for drivers: exactly one PI DMA is needed per packet in each direction, which matters because PI DMA is a single, system-wide contended resource (primer §6.4).

## 4. Register side-effect semantics (normative)

- **`IRQ_STATUS`** is write-1-to-clear (W1C): writing a 1 to a bit clears it; writing 0 to a bit leaves it unchanged. This is the only register with W1C semantics.
- **`TX_DOORBELL`**: the value written is ignored; the write itself is the trigger. Effect: the current `TX_BUF`/`TX_LEN`/`TX_DST` contents are committed for transmission. **Busy-write behavior (resolved):** if `STATUS.TX_READY == 0` (a previous TX is still in flight) at the time of the write, the doorbell write is **silently ignored** — the in-flight packet is unaffected, no error is raised, and no `CMD_STATUS`-style feedback is produced, because `TX_DOORBELL` is a write-only register with no side channel for reporting rejection. The driver's contract is to check `STATUS.TX_READY` (or wait for the `TX_DONE` IRQ) before ringing the doorbell again; this rule keeps the register legal to implement on hardware with no error-reporting path for a WO register.
- **`RX_CONSUME`**: the value written is ignored; the write pops the ring head. If the ring is now empty, `RX_LEN` reads as 0 and `RX_COUNT` reads as 0.
- **`CMD`**: writing an opcode while `CMD_STATUS.CODE` is `BUSY` is illegal; the implementation MUST set `CMD_STATUS.CODE = ERROR`, `CMD_STATUS.ERRNO = EBUSY` (§3.4.1) for the *new* write, leaving the in-flight command's eventual completion unaffected — golden-tested in `netpak-core` (integration doc §11).
- **Low-half-completes-the-word rule**: for every 32-bit register above, the described side effect (doorbell commit, W1C, CMD dispatch, RX_CONSUME pop) fires when the **low half** write completes. This makes a 32-bit CPU store, a PI DMA burst, and two sequential 16-bit stores behave identically — the property that lets this same semantics transfer unmodified to a future 16-bit-native hardware implementation (integration doc §5.2).
- **Overflow policy**: if the RX ring is full when a new packet would be queued, the implementation drops the **oldest** queued packet, not the new one (drop-oldest). Rationale: for live game state, the newest packet is worth more than the oldest.

## 5. Command set (mailbox protocol)

Commands execute asynchronously via the `CMD`/`ARG*`/`RES*`/`CMD_DATA` mailbox: write `CMD` → `CMD_STATUS.CODE` transitions to `BUSY` → (device does any necessary network round-trip) → `CMD_STATUS.CODE` transitions to `OK` or `ERROR` (with `ERRNO` set) → `RES0`–`RES3` hold results, `IRQ_STATUS.EVENT` is set if unmasked.

| Opcode | Command | Arguments | Results | Notes |
|---|---|---|---|---|
| `0x00` | `NOP` | — | — | No-op; useful for driver liveness checks |
| `0x01` | `SET_IDENTITY` | `CMD_DATA` = identity string (§5.1) | — | Called once during `netpak_init()` |
| `0x02` | `SESSION_CREATE` | — | `RES0` = node id | `CMD_DATA[0:6]` holds the assigned join code on completion (§5.1). **Move semantics:** issued while in a session, the current room is implicitly left first |
| `0x03` | `SESSION_JOIN` | `CMD_DATA` = join code (§5.1) | `RES0` = node id | Move semantics, as above. v0.9 relays treat JOIN as join-or-create (code knowledge = permission; no lobby) |
| `0x04` | `SESSION_LEAVE` | — | — | |
| `0x05` | `LIST_PEERS` | — | `RES0` = peer count | Peer entries in `CMD_DATA` (§5.1) |
| `0x06` | `PING` | `ARG0` = target node id | `RES0` = RTT in **microseconds** (u32) | |
| `0x07` | `GET_TIME` | — | `RES0` = high 32 bits, `RES1` = low 32 bits of a u64 relay clock, in **microseconds since the relay process started** (not wall-clock/Unix epoch) | Same semantics as the wire `TIME` frame, §8.6 |
| `0x08` | `GET_STATS` | — | `RES0` = tx_packets, `RES1` = rx_packets, `RES2` = rx_dropped (ring-overflow count, §4), `RES3` = reserved (`0` in v0.9) | All counters u32, saturating |
| `0x09`–`0xFE` | Reserved | | | For future commands within v0.x minor revisions |
| `0xFF` | Reserved | | | Held open as a future "extended opcode" escape; must not be assigned a meaning in v0.x |

### 5.1 CMD_DATA substructure formats

**Identity / peer name strings:** ASCII, maximum 15 characters + null terminator = **16 bytes fixed**. `SET_IDENTITY`'s `CMD_DATA` is exactly this 16-byte field (bytes 16–511 unused/undefined for this command).

**Join codes:** exactly **6 characters**, drawn from the alphabet `ABCDEFGHJKLMNPQRSTUVWXYZ23456789` (32 symbols; excludes `0`/`O` and `1`/`I` to avoid visual ambiguity when players read codes aloud or off a TV). ~30.6 bits of entropy per code. Input is case-insensitive; the relay normalizes to uppercase before comparison. `SESSION_JOIN`'s `CMD_DATA[0:6]` holds the 6 characters; `SESSION_CREATE` writes the assigned code to `CMD_DATA[0:6]` followed by a null terminator at `CMD_DATA[6]` for driver convenience.

**Peer entry** (used by `LIST_PEERS`), 24 bytes each, packed starting at `CMD_DATA[0]`:

```
struct PeerEntry {          // 24 bytes, 8-byte aligned
    u8  node_id;
    u8  reserved0;
    u16 reserved1;
    u32 rtt_us;              // last-measured RTT to this peer, microseconds
    u8  name[16];             // ASCII, null-padded, matches SET_IDENTITY format
}
```

`floor(512 / 24) = 21` entries fit in `CMD_DATA`, comfortably above the 8-node room cap (§8.8). `RES0` (peer count) will never exceed 8 in a v0.9 room but the buffer is sized with headroom.

## 5.2 Driver implementation notes (informative, learned from the reference driver)

- **libdragon PI DMA:** `dma_read`/`dma_write` mangle their PI address into the
  cart-ROM range (`0x1000_0000+`) — a documented historical quirk of libdragon.
  A driver for this device (which sits at `0x05F0_0000`) MUST use
  `dma_read_raw_async`/`dma_write_raw_async` + `dma_wait`. The alignment rules
  those primitives impose (RDRAM 8-byte, PI 2-byte, length multiple of 2) are
  already satisfied by this spec's §1 buffer conventions. Symptom of getting
  this wrong: silently reading/writing cart-ROM space; on ares specifically,
  reads return an address-echo pattern.
- **Cache-line granularity trumps DMA granularity:** `data_cache_hit_invalidate`
  requires **16-byte** address and length granularity (asserting otherwise —
  a partial-line invalidate would destroy neighboring dirty data). Drivers
  should pad all buffer transfers to 16 bytes, not the DMA-minimum 8. A trap
  worth knowing: any payload that happens to be a multiple of 16 hides the bug.
- **Guard every register access against PI busy:** the PI holds a bus write
  in flight for hundreds of cycles; a register READ in that window returns the
  write latch (not the register) and a second WRITE is dropped. libdragon's
  `io_read`/`io_write` do not guard this. A register driver naturally emits
  write→read pairs (`RX_CONSUME`→`RX_COUNT`, doorbell→`STATUS`), so wrap all
  device register access with a PI-idle wait (`dma_wait()`), as the reference
  driver does.
- **Timeouts:** a ROM's clock (`get_ticks_us()`) is *emulated* time; relay
  round-trips happen in *host wall-clock* time, and emulation may run slower
  than realtime. Drivers should be generous with connection/handshake
  timeouts (seconds of emulated time).

## 6. Interrupts

RX-ready, TX-done, and link/peer events assert the **cartridge interrupt line** — CPU CAUSE bit **IP3**, the same line the 64DD uses (primer §5). This is a deliberate reuse: the line already exists in every emulator that models the DD, and on both physical connectors (cartridge and EXT port carry the same PI bus).

- **Trigger model:** level-triggered. `irqLine = OR(IRQ_STATUS AND IRQ_MASK)` — i.e. the line is asserted whenever any set `IRQ_STATUS` bit has its corresponding `IRQ_MASK` bit also set, and deasserts the instant that's no longer true (via W1C or mask changes).
- **Driver responsibility:** a driver registers a cart-IRQ handler where libdragon provides one, or falls back to `netpak_poll()` pumping the same logic synchronously. `libnetpak` supports both (`docs/architecture/05-component-libnetpak.md`).
- **Bus timing correctness:** because Dom2 timing is software-programmed (§2.1) rather than ROM-header-seeded, the emulator's or hardware's PI-transfer pacing tracks whatever the driver configured — device timing behavior is a spec constant, not an emulator approximation.

## 7. VERSION / CAPS negotiation

The device exposes `VERSION` (major.minor) and `CAPS` (§3.1) as read-only registers from the moment it's detected, **before** any command is issued. This exists so that a future hardware-driven spec revision does not strand ROMs built against an earlier `libnetpak`:

- A driver MUST check `VERSION.major` against the range it supports and refuse to proceed (report "unsupported device") rather than assume compatibility.
- `CAPS` communicates optional capabilities (transport variants, max peer count, buffer size variants) without requiring a major version bump for every hardware-side extension.
- Minor version increments MUST be backward compatible: new registers/commands may be added at unused offsets/opcodes, but existing register offsets, widths, and side effects MUST NOT change meaning within a major version.

This spec version corresponds to device `VERSION = 0x0000_0000` (major 0, minor 0) — a pre-1.0 device identity, expected to change once this document reaches v1.0 (§9).

## 8. Wire protocol (relay UDP frame format)

This section specifies the protocol spoken between a `NetClient` (device-side net thread, or firmware equivalent) and `np64-relay`, and between relay-connected peers. Unlike §1–§7, this section does not describe device-visible state — it is internal to `netpak-core`'s `NetClient` and to the relay.

### 8.1 Frame format

```
struct Frame {
    u8  ver;       // wire protocol version
    u8  type;      // frame type, see §8.2
    u8  flags;     // type-specific flags
    u8  channel;   // 0 = ch0 (unreliable), 1 = ch1 (reliable) — see libnetpak, doc 05
    u32 token;      // per-connection identity token, see §8.3
    u16 seq;        // sequence number (control plane; meaning is type-specific)
    u16 ack;        // acknowledgment number (control plane)
    u8  src;        // source node id
    u8  dst;        // destination node id (0xFF = broadcast to room)
    u16 len;        // payload length in bytes
    u8  payload[len];
}
```

Fixed header size: **16 bytes**, all fields big-endian. Total frame size on the wire (header + payload) MUST NOT exceed **~1200 bytes**, chosen to stay clear of IP fragmentation on typical path MTUs while comfortably wrapping the device's 1024 B payload MTU (§3, `TX_BUF`).

### 8.2 Frame types

| Value | Type | Value | Type |
|---|---|---|---|
| `0x00` | `HELLO` | `0x08` | `PING` |
| `0x01` | `WELCOME` | `0x09` | `PONG` |
| `0x02` | `CREATE` | `0x0A` | `TIME` |
| `0x03` | `JOIN` | `0x0B` | `LEAVE` |
| `0x04` | `JOINED` | `0x0C` | `KICK` |
| `0x05` | `PEER_JOIN` | `0x0D` | `KEEPALIVE` |
| `0x06` | `PEER_LEAVE` | `0x0E` | `ERROR` |
| `0x07` | `DATA` | `0x0F`–`0xFF` | Reserved |

### 8.3 Handshake & identity

1. Client sends `HELLO { proto_ver, identity, room_op }`.
2. Relay responds `WELCOME { token, session, node_id }`.
3. Every subsequent frame from that client carries the 32-bit `token`.

The token serves two purposes on a connectionless transport: **spoof resistance** (frames without a valid token are dropped before any further work) and **connection identity across address changes**. The relay **re-pins** a client's source `addr:port` to the latest observed address on any frame bearing a valid token — this is what allows a mid-session NAT rebind (common on consumer routers) to heal transparently, with no reconnect and no game-visible event. See `docs/architecture/07-dynamic-views.md` §7.5 for the full sequence.

### 8.4 Control plane (reliable)

`CREATE`, `JOIN`, `LEAVE`, `LIST_PEERS`-equivalent, `TIME`, `PING` use `seq`/`ack` with retransmit:

| Parameter | Value |
|---|---|
| Initial RTO | ≈250 ms |
| Backoff | Doubling |
| Window | **4** outstanding unacked control frames |

The window is deliberately small and fixed-size: control-plane traffic is low-volume (session operations, not game data), so 4 in flight is enough to pipeline a burst of startup operations (e.g. `CREATE` followed by a couple of `PING`s) without serializing everything to stop-and-wait, while staying implementable as a small fixed array with no dynamic allocation.

Handlers MUST be **idempotent** — a retransmitted `JOIN` that already succeeded returns the same `JOINED` response rather than erroring, which is what makes blind retransmission safe without exactly-once delivery machinery.

### 8.5 Data plane (fire-and-forget)

`DATA` frames on `ch0` are forwarded by the relay **as-is**, with no buffering and no acknowledgment — pure star-topology forwarding, unicast by `dst` node id or broadcast when `dst = 0xFF`. `ch1` reliability (sliding window, retransmit) is implemented **end-to-end in `libnetpak`**, not by the relay — see `docs/architecture/05-component-libnetpak.md` §3 and the sequence in `docs/architecture/07-dynamic-views.md` §7.6. The relay never inspects channel-specific semantics for `DATA` frames beyond forwarding them.

### 8.6 Clock service

`TIME`/`PING`/`PONG` frames let a client sample RTT to the relay and obtain a relay-anchored timestamp. The `TIME` frame's payload is an **8-byte big-endian u64**: microseconds since the relay process started (not wall-clock, not Unix epoch — a shared reference all room members can compute offsets against). This is identical in format and units to the `GET_TIME` command result (§5), by design — `libnetpak`'s `netpak_time()` passes the value through without reinterpretation.

### 8.7 Liveness

- `KEEPALIVE` sent every **~10 s** by each connected client, holding NAT bindings open and refreshing relay-side liveness state.
- The relay GCs a node after **~30 s** of silence and emits `PEER_LEAVE` to the rest of the room.
- Idle rooms (no connected nodes) are reaped after **300 s (5 min)** of zero connected nodes by default. This is a **relay-local operational policy, not a wire-protocol constant** — no client ever observes it directly, so it's exposed as relay config (`np64-relay --idle-room-timeout=<seconds>`) rather than pinned here. The 300 s default is chosen to survive a brief "everyone alt-tabs" style all-disconnect without losing the room/code, while bounding memory growth from abandoned rooms.

### 8.8 Rooms

- Join codes are **6 characters** from the alphabet defined in §5.1.
- Maximum **8 nodes** per room (realistic homebrew usage: 2–4).
- Presence is communicated via `PEER_JOIN`/`PEER_LEAVE` frames broadcast to the room.

### 8.9 Security posture (v1)

v1 is **plaintext UDP** authenticated only by the per-connection random token (§8.3) and unguessable room codes (§8.8) — appropriate for the "friends playing homebrew" threat model, not for an on-path adversary. Relay-side abuse controls that ARE in v1 scope: per-token rate limiting, frame size caps (§8.1), room GC (§8.7), no public lobby/room listing. DTLS ([RFC 9147](https://www.rfc-editor.org/rfc/rfc9147)) or a [Noise](https://noiseprotocol.org)-based layer are the named v2 hardening paths (out of scope for this version of the spec).

## 9. Versioning and change policy for this document

- This file's own version (currently **v0.9**) tracks the frozen-ness of the *device and wire spec*, and is independent of the device's own `VERSION` register (§7) — the register communicates the frozen protocol version to running software; this document's version communicates how stable the spec text itself is.
- **v0.9 → v1.0** requires (a) all values in this document to be resolved — **done as of this revision**, with the Dom2 timing constants (§2.1) explicitly flagged as provisional rather than blocking — and (b) the P0 exit criterion in `ROADMAP.md` ("spec doc reviewed") to be satisfied, which is a separate, explicit sign-off step this revision does not itself constitute.
- Post-v1.0 changes that alter existing register offsets, widths, side effects, wire frame layout, or existing opcode/type/errno numeric values are **breaking** and require a device `VERSION` major bump (§7). Purely additive changes (new registers at unused offsets, new opcodes, new frame types, new errno values) are minor-version-compatible.
- The §2.1 Dom2 timing values may be updated post-v1.0 **without** a major version bump specifically because software (not a frozen convention) owns them — a driver always reads them from this document/`netpak_init()`, never assumes a value, so tuning them is not an interface change. This is the one documented exception to "post-v1.0 value changes are breaking."
- Changes to this document should be cross-checked against `docs/architecture/04-component-ares-glue.md` §5 (glue's register-level assumptions) and `docs/architecture/03-component-netpak-core.md` (the reference implementation) — see `ROADMAP.md` re-sync obligations.
