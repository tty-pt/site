# Site & NeverDark Ecosystem

Pure-C web platform and NeverDark MUCK universe built on the **axil** HTTP/WebSocket library, the **libxylem** XY module system, the **hyle** data engine, and the **bud** HTML/DOM builder.

> **Jump Directly to What You Need:**
> - 🚀 **[Situation A: Turnkey Setup (Site + 32-Planet Universe)](#situation-a-turnkey-deployment-site--32-planet-universe)** — Install all libs, seed a 32-planet sector with 3D WebGL showcase, and run everything on `:8080`.
> - 🛠️ **[Situation B: Clean / Unseeded Setup (In-Game Worldbuilding)](#situation-b-clean--unseeded-deployment-in-game-worldbuilding)** — Start with a bare database and build planets, rooms, and items in-game.
> - 🎵 **[Situation C: Music Platform Only](#situation-c-lightweight-music-platform-only)** — Run just the music platform dev server (`poem`, `song`, `grp`, `gig`).
> - 🧊 **[Showcase: The 3D WebGL World (Planet 2: Aethelgard)](#showcase-the-3d-webgl-world-planet-2-aethelgard)** — Explore the hardware-accelerated 3D multi-room bastion.
> - 📚 **[Documentation Library](#documentation-index)** — 18 deep-dive architectural specifications and manuals.
> - 🧪 **[Testing & Quality Gates](#testing--quality-gates)** — Automated E2E universe testing and boundary audits.

---

## Deployment Recipes

### Situation A: Turnkey Deployment (Site + Seeded Universe)

Prepare a complete pre-seeded NeverDark star sector with all tenant modules active, and run the site using `./start.sh`:

```bash
# 1. Build platform modules and client WASM assets
make

# 2. Launch the site (singular turnkey command: auto-seeds universe.db if missing, sets library paths, launches on :8080)
./start.sh
```

*(Alternatively, to manually seed custom sectors:* `make seed-universe` *or* `ND_PLANETS=32 ND_DB=var/nd/universe.db make seed-universe`*)*

### Multi-Instance Isolation (e.g. Site on :8080 vs Auren on :4242)
Running multiple axil instances simultaneously on the same host (e.g. site on `:8080` and Auren on `:4242`) is fully isolated:
- **Port-Scoped Cookies**: Session cookies default to `QSESSION` on standard ports (80/443/8080) and `QSESSION_<port>` (e.g. `QSESSION_4242`) on custom ports, with graceful `QSESSION` fallback. `AXIL_COOKIE_NAME` can override this explicitly.
- **Client-Side WebSocket Bus**: `window.__axil_ws` is isolated and namespaced by host/port origin, preventing browser tabs on different ports from cross-talking or disconnecting each other.

#### What you get on `http://localhost:8080`:
- **Music Platform** (`/`): Songs, chord transposer, setlists, groups, and poem library.
- **NeverDark MUCK Universe** (`/nd`): 
  - Dynamic pure-C SSR paint and instant hydration.
  - Interactive **3D WebGL**, **2D Lamina tilemap**, and **ANSI TTY** viewports.
  - Central spawn at **Planet 1: The Citadel** (`room 0 0 0 1`) with merchant shop, training golem, and starter gear.
  - **Planet 2: Aethelgard 3D Bastion** (`room 0 0 0 2`) showcasing 3D multi-room perspective geometry and dynamic lighting.
- **Terminal Telnet CLI**:
  ```bash
  nc 127.0.0.1 8080
  ```
  Commands: `look`, `inventory`, `stats` (`nd-attr`), `help`, `modlist 1`, `teleport 0 0 0 2`.

---

### Situation B: Clean / Unseeded Deployment (In-Game Worldbuilding)

Run the full platform with a completely clean, unseeded database where world builders and players establish planets, rooms, and items in-game via MUCK commands:

```bash
# 1. Install all runtime libraries
sudo make install-libs

# 2. Clear any pre-seeded databases to guarantee a blank slate
rm -f var/nd/*.db

# 3. Build and launch the full platform in development mode
make run
```

#### In-Game Worldbuilding:
Connect as the builder via browser at [`http://localhost:8080/nd`](http://localhost:8080/nd) or via terminal (`nc 127.0.0.1 8080`):
```text
connect Admin admin123        # Login as universe administrator
planet 1                      # Claim and establish World 1
room 0 0 0 1                  # Create root room at coordinates (0, 0, 0, 1)
teleport 0 0 0 1              # Enter the room
name The Star Keep            # Name the room
create Vorpal Blade           # Create custom entity
drop Vorpal Blade             # Place item in room
room 1 0 0 1                  # Dig East room at (1, 0, 0, 1)
save                          # Persist universe to disk
```

---

### Situation C: Lightweight Music Platform Only

To run the music platform alone with auto-rebuild and live reload on code changes:

```bash
make dev            # starts dev server on :8080 (auto-cleans stale server & builds)
```
Or with auto-rebuild file watcher:
```bash
make watch          # auto-rebuild + restart on :8080 (requires entr)
```

---

## Showcase: The 3D WebGL World (Planet 2: Aethelgard)

NeverDark features an in-WASM hardware-accelerated 3D scene engine ([`mods/nd/ux/nd_webgl.c`](./mods/nd/ux/nd_webgl.c)) running alongside the 2D tile blitter.

When running Situation A (`make run-sector32`), **Planet 2 ("Aethelgard")** is pre-configured as a multi-room 3D architectural showcase:
- **(0, 0, 0, 2) — The High Crystal Sanctum**: Central hall with floating Crystal Core and open archways in all 4 cardinal directions.
- **(1, 0, 0, 2) — The Observatory Terrace**: High-altitude terrace overlooking the stellar void with Star Tracker fixture.
- **(0, 1, 0, 2) — The Luminescent Spire**: Illuminated tower holding the Sun Prism relic.
- **(0, -1, 0, 2) — The Shadow Vault**: Subterranean vault guarded by an Obsidian Sentinel mob.

#### How to Experience It:
1. Open [`http://localhost:8080/nd`](http://localhost:8080/nd) in your browser.
2. In the viewport selector bar, click **`🧊 WebGL`**.
3. Type `teleport 0 0 0 2` in the input bar (or use the Compass Keypad).
4. Observe the perspective 3D room boxes, cutaway exit archways, stylized 3D avatar prisms, target highlight halos, and dynamic sun/sky ambient lighting driven by Time-of-Day (`tod`).

---

## Architecture & Core Invariants

- **Single-Port Unified Service**: HTTP GET, SSR HTML, static assets (`/nd.wasm`, `/bud-client.js`, `/styles.css`), and full-duplex WebSocket upgrades are multiplexed on port `:8080` in a single process.
- **SSR-First Isomorphic Architecture**: Pure-C UX components (`mods/*/ux`) compile twice — as native `.so` shared modules for instant Server-Side Rendering (SSR) and into WebAssembly (`htdocs/*.wasm`) for browser enhancement.
- **Zero Foreign Runtime in the Request Path**: Pure C on the server and pure C in WASM. No Rust, Dioxus, Deno, or Node in the request pipeline.
- **Strict Module Encapsulation**: Strict two-header separation (`*-types.h` and `*.h`), zero `*_IMPL` guards, and cross-module communication solely via `libxylem` XY dispatch.
- **Declarative Zero-Arithmetic BCP**: Dual-tier wire protocol ([`external/axil-nd/include/nd/bcp-meta.h`](./external/axil-nd/include/nd/bcp-meta.h)) with zero-copy packed memory-mapped structs and X-macro schema descriptors.

---

## Documentation Index

The repository maintains comprehensive architectural specifications and manuals under [`docs/`](./docs/):

### Foundations & Quick Reference
| Document | Focus |
| :--- | :--- |
| **[`docs/OVERVIEW.md`](./docs/OVERVIEW.md)** | **5-minute orientation**: technology stack, framework-pair model, request path, and invariants. |
| **[`docs/BUILD.md`](./docs/BUILD.md)** | Toolchain installation, native and WASI compilation, chroot execution, and dependencies. |
| **[`docs/GOALS.md`](./docs/GOALS.md)** | Core system goals, structural invariants, and regression checklists. |
| **[`AGENTS.md`](./AGENTS.md)** | Developer index, cross-module boundary rules, and development conventions. |

### Architecture & System Design
| Document | Focus |
| :--- | :--- |
| **[`docs/ARCHITECTURE.md`](./docs/ARCHITECTURE.md)** | Dynamic module graph (`mods.load`), load order, XY hook contract, and data integrity. |
| **[`docs/DESIGN.md`](./docs/DESIGN.md)** | Component encapsulation, abstraction barriers, and architectural boundaries. |
| **[`docs/CONVENTIONS.md`](./docs/CONVENTIONS.md)** | C coding style, request handlers, form parsing, XY macros, and preprocessor constraints. |

### NeverDark MUCK & 3D WebGL Subsystem
| Document | Focus |
| :--- | :--- |
| **[`docs/NEXT_GEN_ND_ARCHITECTURE.md`](./docs/NEXT_GEN_ND_ARCHITECTURE.md)** | Master specification: single-socket WebSocket multiplexer, declarative BCP framework, reactive 3D/2D scene synchronizer, and SSR state hydration. |
| **[`docs/NEVERDARK_FULL_UNIVERSE_DEPLOYMENT.md`](./docs/NEVERDARK_FULL_UNIVERSE_DEPLOYMENT.md)** | Operational deployment guide: single-port architecture, seeder, and automated E2E test harness. |
| **[`docs/ND-VIEWPORTS.md`](./docs/ND-VIEWPORTS.md)** | Viewport drivers: ANSI TTY terminal, 2D Lamina tilemap, and WebGL 3D scene engine. |

### Frontend & Isomorphic UX
| Document | Focus |
| :--- | :--- |
| **[`docs/WEB_HAL_SPECIFICATION.md`](./docs/WEB_HAL_SPECIFICATION.md)** | Web Hardware Abstraction Layer: systems C/WASM boundary, 60fps frame loop, and multi-modal viewports. |
| **[`docs/WEB_HAL_IMPLEMENTATION_PLAN.md`](./docs/WEB_HAL_IMPLEMENTATION_PLAN.md)** | Sequenced rollout roadmap: Web HAL bridges, frame loop contracts, and NeverDark/site harmonization. |
| **[`docs/STYLING.md`](./docs/STYLING.md)** | CSS design tokens, asset versioning, and cache busting. |

### Data Engine & Schemas
| Document | Focus |
| :--- | :--- |
| **[`docs/SCHEMA.md`](./docs/SCHEMA.md)** | Canonical `hyle_schema_desc_t` data descriptors and validation rules. |
| **[`docs/FILTERS.md`](./docs/FILTERS.md)** | Full-text search (FTS), term interning, and schema hints via `libhyle-bud`. |
| **[`docs/PICKERS.md`](./docs/PICKERS.md)** | Omni-dropdowns, asynchronous option search, and keyboard navigation. |

### Testing & Operations
| Document | Focus |
| :--- | :--- |
| **[`docs/TESTING.md`](./docs/TESTING.md)** | Unit, integration, and Playwright end-to-end testing requirements. |
| **[`docs/EXTENSIONS.md`](./docs/EXTENSIONS.md)** | Pi coding agent extension workflows and custom agent commands. |
| **[`docs/ADVANCED_GIT_RECOVERY.md`](./docs/ADVANCED_GIT_RECOVERY.md)** | Git recovery workflows, reflog reconstruction, and submodule state preservation. |

---

## Testing & Quality Gates

The codebase enforces strict test suites and architectural boundaries:

```bash
make test                   # comprehensive test gate: boundaries, unit, pages, integration, and E2E suites
make test-fast              # fast matrix, slugify, i18n, BCP, and pages smoke tests
make test-e2e               # runs all 121 Playwright browser end-to-end test suites
make test-universe          # runs automated headless E2E full-universe test suite
make standalone-unit-tests  # runs C unit tests (BCP meta, scene sync, hydration, ws multiplex)

# Targeted NeverDark E2E Testing:
./scripts/run-with-server.sh deno test --allow-all tests/e2e/nd-ws-roundtrip.test.ts
./scripts/run-with-server.sh deno test --allow-all tests/e2e/nd-browser-post-seed.test.ts
```

### Boundary & Purity Verification
Automated scripts guarantee architectural compliance:
```bash
sh scripts/check-wasm-imports.sh        # ensures zero forbidden native symbols in WASM
sh scripts/check-no-impl-guards.sh       # verifies zero *_IMPL guards & two-header separation
sh scripts/check-ux-purity.sh           # verifies pure-C isomorphic UX with zero XY hooks
sh scripts/check-no-site-specific-js.sh  # verifies htdocs/*.js has zero domain-specific code
sh scripts/check-module-boundaries.sh   # enforces strict cross-module encapsulation
```

---

## Modules Directory

### Core Web Platform (`mods/`)
- **`/core`** — Bootstrap DAG, root router, module loader, and core redirects.
- **`/auth`** — Session lifecycle, password authentication, and CSRF protection.
- **`/common`** — Shared response helpers, layouts, and declarative UI builders (`site_ui_form_from_desc`, `site_ui_picker`).
- **`/source`** — Dataset registration and HTTP API transport over `libhyle-source`.
- **`/index`** — Generic CRUD routes, list views, and picker option endpoints.
- **`/poem`** — Poem upload, display, and listing.
- **`/song`** — Song detail, chord charts, transposition, and media attachments.
- **`/grp`** — Group repertoire management and format categories.
- **`/gig`** — Gigs and setlists with transpose settings and format tags.
- **`/mpfd`** — Streaming multipart/form-data parser.
- **`/site_chrome`** — WASM client chrome enhancement (`htdocs/site_chrome.wasm`).
- **`/nd`** — Isomorphic frontend reactor for NeverDark MUCK (`mods/nd/ux/` $\rightarrow$ `nd.so` & `nd.wasm`) with pluggable TTY, Lamina 2D, and WebGL 3D viewports.

### Core Infrastructure Submodules (`external/`)
- **`axil`** — Single-port HTTP/WebSocket network daemon and transport multiplexer.
- **`libxylem`** — Zero-cost C function dispatch bus (`XY_DECL`, `XY_IMPL`, `XY_LISTENER_TAG`).
- **`libcorm`** — Embedded transactional key-value and record storage engine.
- **`libbud`** — Pure virtual DOM tree builder and SSR serializer.
- **`libhyle`** — Relational schema engine, query planner, and full-text search index.
- **`libhyle-bud`** — Bridge connecting Hyle schema hints to Bud UI binders.
- **`liblamina`** — 2D software tile blitter and WebGL hardware acceleration bridge.
- **`axil-nd`** — NeverDark MUCK server engine with 4D spacetime universe simulation.
- **`nd-*`** — 19 gameplay and content tenant modules (`nd-attr`, `nd-equip`, `nd-fight`, `nd-spell`, `nd-shop`, `nd-level`, `nd-mortal`, `nd-race`, `nd-class`, `nd-mob`, `nd-biome`, `nd-plant`, `nd-drink`, `nd-seat`, `nd-stone`, `nd-vanilla`, `nd-wts`, `nd-other`).
