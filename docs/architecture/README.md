# NetPak64 — Architecture Documentation

**Audience:** software engineers and computer engineers joining the project, including readers with **no prior N64 knowledge** (start with the [platform primer](00-n64-platform-primer.md)).
**Relationship to other documents:** this set *expands* `netpak64-proposal.md` (v0.2) and `netpak64-ares-integration.md` (v0.1) into reviewable architecture views. Where this set and those docs disagree, the proposal/integration docs win until this set is re-synced; note the discrepancy in `ROADMAP.md`.

## Document map

| Doc | C4 level / view | What it answers |
|---|---|---|
| [00 — N64 platform primer](00-n64-platform-primer.md) | Background | How the N64's CPU, memory, PI bus, DMA, caches, and interrupts actually work — everything the rest of the set assumes |
| [01 — System context](01-system-context.md) | C4 L1 | Who uses NetPak64, what the system boundary is, what's external, how we relate to adjacent projects |
| [02 — Containers & deployment](02-containers.md) | C4 L2 + deployment | The processes/libraries/threads, what runs where (dev loop vs. production), and the protocols between them |
| [03 — netpak-core components](03-component-netpak-core.md) | C4 L3 | Internals of the standalone device library: register file, command FSM, rings, net client, backends |
| [04 — ares glue components](04-component-ares-glue.md) | C4 L3 | The thin in-emulator adapter: bus decode, tick event, IRQ line, serialization |
| [05 — libnetpak components](05-component-libnetpak.md) | C4 L3 | The ROM-side driver: probe, register access, IRQ handler, channels, session API |
| [06 — np64-relay components](06-component-np64-relay.md) | C4 L3 | The relay service: sessions, rooms, control/data planes, liveness, capacity |
| [07 — Dynamic views](07-dynamic-views.md) | Sequences | Probe/init, session join, end-to-end datagram, savestate/epoch, NAT rebind, reliable channel |
| [08 — References](08-references.md) | Bibliography | Consolidated third-party references cited throughout |

PlantUML sources live in [`diagrams/`](diagrams/); each narrative doc embeds its diagrams by file reference.

## Rendering the diagrams

All diagrams are [PlantUML](https://plantuml.com) using the [C4-PlantUML](https://github.com/plantuml-stdlib/C4-PlantUML) macros from the PlantUML standard library (`!include <C4/C4_Context>` etc. — no network access needed with a current PlantUML jar).

```sh
# All diagrams to SVG
plantuml -tsvg docs/architecture/diagrams/*.puml

# Or point https://kroki.io / the VS Code "PlantUML" extension at any .puml
```

If your PlantUML is too old for stdlib C4, replace the include with the raw-URL form documented in the C4-PlantUML README.

## Conventions

- **C4 model** ([c4model.com](https://c4model.com), Simon Brown): Context → Container → Component. We stop at L3; L4 (code) is the source itself plus the spec.
- Addresses are physical N64 bus addresses unless prefixed KSEG1 (`0xA…`); see the primer for segment mapping.
- "Device" always means the NetPak64 register-level device (virtual today, hardware later). "Adapter" means the future physical implementation.
- Register/protocol values quoted here mirror proposal §5 and §8 (strawman v0); they freeze as `netpak-spec.md` v0.9 in phase P0.
