# NetPak64 Architecture Website

## Overview

This directory contains a complete web-browsable architecture documentation site for NetPak64, built from PlantUML C4 diagrams and detailed markdown specifications.

## Files

```
docs/web/
├── index.html              # Project overview & primer
├── 00-primer.html          # N64 platform background (no prior knowledge assumed)
├── 01-context.html         # C4 Level 1: System context & actors
├── 02-containers.html      # C4 Level 2: Container & deployment views
├── 03-components.html      # C4 Level 3: netpak-core, ares glue, libnetpak, relay
├── 04-dynamic.html         # Sequence diagrams: probe, join, data flow, savestate, NAT, ch1
├── 05-references.html      # Consolidated bibliography
├── assets/
│   └── style.css           # Professional styling (dark header, clean typography, responsive)
└── README.md               # This file
```

## Viewing the site

### Local browsing (recommended)

Open `index.html` in any modern web browser. All pages link to each other; navigation is in the sticky top bar.

```bash
# From command line (works on most systems):
open docs/web/index.html          # macOS
xdg-open docs/web/index.html      # Linux
start docs/web/index.html         # Windows (cmd)
# or just double-click index.html in Explorer/Finder
```

### Live preview

If you have Python 3 installed, run a local server to avoid CORS issues with embedded SVGs:

```bash
cd docs/web
python -m http.server 8000
# Then visit http://localhost:8000 in your browser
```

## Architecture diagrams

All 13 diagrams are rendered as SVG (fully scalable, searchable vector graphics):

| Diagram | Location | View |
|---------|----------|------|
| C4 Context | `../architecture/diagrams/c4-context.svg` | `01-context.html` |
| C4 Containers | `../architecture/diagrams/c4-containers.svg` | `02-containers.html` |
| C4 Deployment | `../architecture/diagrams/c4-deployment.svg` | `02-containers.html` |
| netpak-core components | `../architecture/diagrams/c4-component-netpak-core.svg` | `03-components.html` |
| ares glue components | `../architecture/diagrams/c4-component-ares-glue.svg` | `03-components.html` |
| libnetpak components | `../architecture/diagrams/c4-component-libnetpak.svg` | `03-components.html` |
| np64-relay components | `../architecture/diagrams/c4-component-relay.svg` | `03-components.html` |
| Probe & init sequence | `../architecture/diagrams/seq-probe-init.svg` | `04-dynamic.html` |
| Session join sequence | `../architecture/diagrams/seq-session-join.svg` | `04-dynamic.html` |
| End-to-end datagram | `../architecture/diagrams/seq-data-path.svg` | `04-dynamic.html` |
| Savestate & epoch | `../architecture/diagrams/seq-savestate-epoch.svg` | `04-dynamic.html` |
| NAT rebind & liveness | `../architecture/diagrams/seq-nat-rebind.svg` | `04-dynamic.html` |
| ch1 reliable channel | `../architecture/diagrams/seq-ch1-reliable.svg` | `04-dynamic.html` |

**Source files:** All `.puml` files in `../architecture/diagrams/` use PlantUML C4-PlantUML stdlib and are syntax-validated. To re-render them:

```bash
# Requires Java + PlantUML jar (https://plantuml.com/download)
plantuml -tsvg ../architecture/diagrams/*.puml
```

## Navigation structure

**Recommended reading order:**

1. **Start here:** `index.html` (project overview, quick wins, status)
2. **Platform background:** `00-primer.html` (N64 architecture, PI bus, caches, interrupts)
3. **System view:** `01-context.html` (actors, boundary, positioning vs. related work)
4. **Container view:** `02-containers.html` (processes, threads, what runs where)
5. **Component internals:** `03-components.html` (netpak-core, ares glue, libnetpak, relay)
6. **Dynamics:** `04-dynamic.html` (probe, join, send/recv, savestate, NAT, reliability)
7. **References:** `05-references.html` (N64brew, libdragon, RFCs, papers)

**Quick jump links:** Each page has a "quick nav" footer to jump between pages.

## Content structure

This site expands — rather than replaces — the design docs at `../`:

| Document | Content | Audience |
|----------|---------|----------|
| `netpak64-proposal.md` v0.2 | Product pitch, goals, spec strawman, relay protocol, phases, risks | Decision makers, PMs |
| `netpak64-ares-integration.md` v0.1 | Ares-specific: decode, threads, serialization, recon checklist | Ares integration developers |
| **This web site** | C4 architecture views, N64 primer, sequences, references | All engineers |

**Precedence rule:** On conflict between this site and the proposal/integration docs, the proposal/integration docs win until this site is re-synced.

## Design decisions encoded in the site

1. **Spec-as-contract.** The register-level device spec on the PI bus is the durable contract; this is why the same ROM works across emulator and future hardware.
2. **Three constraints.** Zero behavioral diff when disabled, emulation-thread state ownership, thin reusable glue — everything downstream flows from these.
3. **Hardware-plausible spec.** Trivial decode, software-programmed timing, single-packet TX, exposed-head RX — every choice stays implementable by an MCU/CPGA.
4. **Device-level, not input-lockstep.** Games implement their own netcode (state sync, prediction, lockstep) against a real device, not synchronized inputs.

## Styling notes

- **Color scheme:** Dark blue header (`#1a1a2e` / `#16213e`), accent `#0066cc`, clean sans-serif typography
- **Responsive:** Mobile-friendly; nav wraps to two columns on small screens
- **SVG diagrams:** Embedded directly; zoom/pan natively in browser
- **Code styling:** Inline `<code>` with background, `<pre>` blocks with left border accent

## For developers: updating the site

If you edit the architecture docs in `../`, rebuild this site:

1. **Markdown → HTML:** Each `.md` file has a corresponding `.html` in this directory. Update the HTML by copying relevant sections from the markdown.
2. **Diagrams:** If you modify a `.puml` file, re-render it to `.svg`:
   ```bash
   plantuml -tsvg ../architecture/diagrams/c4-context.puml
   ```
3. **Consistency:** Check all links and cross-references. Use the same section headings as the markdown originals.

## Browser compatibility

Tested on:
- Chrome/Chromium 120+
- Firefox 121+
- Safari 17+
- Edge 120+

Embedded SVGs require modern browser support (no IE11).

## Future expansions

- Search bar across all pages
- Dark mode toggle
- Print-to-PDF stylesheet
- Collapsible TOC for long pages
- Embedded PlantUML viewer for interactive diagram editing
- Exportable architecture snapshot (tarball of HTML + SVGs)
