# Site & NeverDark Ecosystem

Pure-C web platform and NeverDark MUCK universe built on the **axil** HTTP/WebSocket library, the **libxylem** XY module system, the **hyle** data engine, and the **bud** HTML/DOM builder.

> **Jump Directly to What You Need:**
> - 🚀 **[Situation A: Turnkey Setup (Site + 32-Planet Universe)](#situation-a-turnkey-deployment-site--seeded-universe)** — Install all libs, seed a 32-planet sector with 3D WebGL showcase, and run everything on `:8080`.
> - 🛠️ **[Situation B: Clean / Unseeded Setup (In-Game Worldbuilding)](#situation-b-clean--unseeded-deployment-in-game-worldbuilding)** — Start with a bare database and build planets, rooms, and items in-game.
> - 🎵 **[Situation C: Music Platform Only](#situation-c-lightweight-music-platform-only)** — Run just the music platform dev server (`poem`, `song`, `grp`, `gig`).
> - 🧊 **[Showcase: The 3D WebGL World (Planet 2: Aethelgard)](#showcase-the-3d-webgl-world-planet-2-aethelgard)** — Explore the hardware-accelerated 3D multi-room bastion.
> - 📚 **[Documentation Library](#documentation-index)** — 39 deep-dive architectural specifications and manuals.
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

NeverDark features an in-WASM hardware-accelerated 2.5D/3D Seven scene engine ([`mods/nd/ux/nd_seven.c`](./mods/nd/ux/nd_seven.c) and [`mods/nd/ux/nd_seven_gl.c`](./mods/nd/ux/nd_seven_gl.c)) running alongside the 2D tile blitter.

When running Situation A (`./start.sh`), **Planet 2 ("Aethelgard")** is pre-configured as a multi-room 3D architectural showcase:
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

- **Single-Port Unified Service**: HTTP GET, SSR HTML, static assets (`/nd.wasm`, `/stele-client.js`, `/styles.css`), and full-duplex WebSocket upgrades are multiplexed on port `:8080` in a single process.
- **SSR-First Isomorphic Architecture**: Pure-C UX components (`mods/*/ux`) compile twice — as native `.so` shared modules for instant Server-Side Rendering (SSR) and into WebAssembly (`htdocs/*.wasm`) for browser enhancement. Zero platform preprocessor branching (`#ifdef __wasm__`) in feature code.
- **Universal WASM Substrate & Clean Library Decoupling**: `external/libbud` serves as a pure C UI tree and VDOM diff engine without runtime slot or browser assumptions; `external/libstele` acts as the domain-agnostic WASM substrate; `htdocs/stele-client.js` provisions dynamic on-demand component loading with isolated linear memory instances per component.
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
| **[`docs/SIBLING_REPOS.md`](./docs/SIBLING_REPOS.md)** | Sibling repositories and auxiliary services (`axil-nd`, `axil-auren`, `axil-resume-search`). |
| **[`AGENTS.md`](./AGENTS.md)** | Developer index, cross-module boundary rules, and development conventions. |
| **[`ROADMAP.md`](./ROADMAP.md)** | Hyle ecosystem & cross-framework convergence roadmap (React/Next.js). |

### Architecture & System Design
| Document | Focus |
| :--- | :--- |
| **[`docs/ARCHITECTURE.md`](./docs/ARCHITECTURE.md)** | Dynamic module graph (`mods.load`), load order, XY hook contract, and data integrity. |
| **[`docs/DESIGN.md`](./docs/DESIGN.md)** | Component encapsulation, abstraction barriers, and architectural boundaries. |
| **[`docs/CONVENTIONS.md`](./docs/CONVENTIONS.md)** | C coding style, request handlers, form parsing, XY macros, and preprocessor constraints. |
| **[`docs/MONOREPO_UNIFIED_WASM_KERNEL.md`](./docs/MONOREPO_UNIFIED_WASM_KERNEL.md)** | Unified client WASM kernel architecture for the pure-C monorepo. |
| **[`docs/NEXT_GEN_WASM_ARCHITECTURAL_PLAN.md`](./docs/NEXT_GEN_WASM_ARCHITECTURAL_PLAN.md)** | Capability bridge distribution, technical debt elimination, and `libcorm`/`libxylem` in WASM. |
| **[`docs/COLOCATED_NODE_WASM_ADAPTERS.md`](./docs/COLOCATED_NODE_WASM_ADAPTERS.md)** | Colocated Node.js N-API and WebAssembly reactor adapters across `@tty-pt/*` packages. |

### NeverDark MUCK, Procedural Terrain & 3D WebGL
| Document | Focus |
| :--- | :--- |
| **[`docs/25D.md`](./docs/25D.md)** | **2.5D Seven continuous 3D terrain specification**: 55° diorama camera, continuous heightfield meshes, and billboard entity anchors. |
| **[`docs/TERRAIN_ROADMAP.md`](./docs/TERRAIN_ROADMAP.md)** | **Procedural 3D terrain & cavern roadmap**: multi-scale topography, subterranean caverns, and LOD pipeline. |
| **[`docs/RECUR_NOISE.md`](./docs/RECUR_NOISE.md)** | **Recursive noise mathematics**: pure-integer spatial subdivision (`noise_fix_v`), dimensional collapse, and octave hierarchy. |
| **[`docs/LOD_ELEVATION_RECURSIVE_NOISE_ANALYSIS.md`](./docs/LOD_ELEVATION_RECURSIVE_NOISE_ANALYSIS.md)** | **LOD elevation & recursive noise blending**: multi-scale octave blending (`chaff_octave_blend_recursive`) and slope analysis. |
| **[`docs/ANALYSIS_TERRAIN_CHUNKS_LOD_STRATOSPHERE.md`](./docs/ANALYSIS_TERRAIN_CHUNKS_LOD_STRATOSPHERE.md)** | **Terrain chunks, LOD smoothing & stratosphere camera**: client chunk merging, `libareole` quadtree culling, and orbital views. |
| **[`docs/MULTI_SCALE_NOISE_AND_GPU_ARCHITECTURE.md`](./docs/MULTI_SCALE_NOISE_AND_GPU_ARCHITECTURE.md)** | **Multi-scale GPU architecture**: client WebGL acceleration, server zero-OpenGL invariant, and stratosphere scaling. |
| **[`docs/WATER_REQUIREMENTS.md`](./docs/WATER_REQUIREMENTS.md)** | **Authoritative water specification**: Beer-Lambert volumetric optical absorption ($L = d / \cos\theta$) and physical parameters. |
| **[`docs/GUIDE_BILLBOARD_SHALLOW_WATER_OPTICS.md`](./docs/GUIDE_BILLBOARD_SHALLOW_WATER_OPTICS.md)** | **Shallow water optical extinction for billboards**: submergence depth calibration and refractive billboard shading. |
| **[`docs/handoff-water-realism.md`](./docs/handoff-water-realism.md)** | **Water realism handoff**: world-anchored pure-C procedural water optics for the NeverDark Seven stage. |
| **[`docs/SHORE.md`](./docs/SHORE.md)** | **Seamless shoreline & bathymetry**: continuous 7×7 depth fields, organic coastlines, and topological boundary invariants. |
| **[`docs/SEVEN_WAYS_COLLISION_ANALYSIS.md`](./docs/SEVEN_WAYS_COLLISION_ANALYSIS.md)** | **Seven-Ways collision map analysis**: 2.5D obstacle modeling and spatial walkability masks. |
| **[`docs/AOT_GAME_CONTENTS_ANALYSIS.md`](./docs/AOT_GAME_CONTENTS_ANALYSIS.md)** | **Ahead-of-Time (AOT) vs. on-room-entry content**: procedural world materialization and persistence trade-offs. |
| **[`docs/ND-PERSISTENCE-LIFECYCLE.md`](./docs/ND-PERSISTENCE-LIFECYCLE.md)** | **World & UX persistence lifecycle**: entity mutation consistency, stone picking, and Corm DB synchronization. |
| **[`docs/AXIL_ND_MCP_BCP_IMPROVEMENT_SPEC.md`](./docs/AXIL_ND_MCP_BCP_IMPROVEMENT_SPEC.md)** | **MCP/BCP protocol modernization**: structured Binary Client Protocol and Model Context Protocol integration for `axil-nd`. |
| **[`docs/NEXT_GEN_ND_ARCHITECTURE.md`](./docs/NEXT_GEN_ND_ARCHITECTURE.md)** | Master specification: single-socket WebSocket multiplexer, declarative BCP framework, reactive 3D/2D scene synchronizer, and SSR state hydration. |
| **[`docs/NEVERDARK_FULL_UNIVERSE_DEPLOYMENT.md`](./docs/NEVERDARK_FULL_UNIVERSE_DEPLOYMENT.md)** | Operational deployment guide: single-port architecture, seeder, and automated E2E test harness. |
| **[`docs/ND-VIEWPORTS.md`](./docs/ND-VIEWPORTS.md)** | Viewport drivers: ANSI TTY terminal, 2D Lamina tilemap, and WebGL 3D scene engine. |

### Frontend & Isomorphic UX
| Document | Focus |
| :--- | :--- |
| **[`docs/WEB_HAL_SPECIFICATION.md`](./docs/WEB_HAL_SPECIFICATION.md)** | Web Hardware Abstraction Layer: systems C/WASM boundary, 60fps frame loop, and multi-modal viewports. |
| **[`docs/WEB_HAL_IMPLEMENTATION_PLAN.md`](./docs/WEB_HAL_IMPLEMENTATION_PLAN.md)** | Sequenced rollout roadmap: Web HAL bridges, frame loop contracts, and NeverDark/site harmonization. |
| **[`docs/STYLING.md`](./docs/STYLING.md)** | CSS design tokens, asset versioning, and cache busting. |

### Data Engine, AI Inference & Multi-Axis Memory
| Document | Focus |
| :--- | :--- |
| **[`docs/SCHEMA.md`](./docs/SCHEMA.md)** | Canonical `hyle_schema_desc_t` data descriptors and validation rules. |
| **[`docs/FILTERS.md`](./docs/FILTERS.md)** | Full-text search (FTS), term interning, and schema hints via `libhyle-bud`. |
| **[`docs/PICKERS.md`](./docs/PICKERS.md)** | Omni-dropdowns, asynchronous option search, and keyboard navigation. |
| **[`docs/LOCAL_LLM.md`](./docs/LOCAL_LLM.md)** | Local LLM inference (`axil-auren`), streaming chat sessions, and Pi agent integration. |
| **[`docs/SEMANTIC_SEARCH.md`](./docs/SEMANTIC_SEARCH.md)** | Custom semantic and hybrid search using `axil-auren`, `libsepal`, `liblemma`, and `libcorm`. |
| **[`docs/MULTI_AXIS_MEMORY_FUSION.md`](./docs/MULTI_AXIS_MEMORY_FUSION.md)** | Multi-axis memory fusion combining dense vectors (`libsepal`), symbolic epistemology (`pi-mm`), and Corm axes (`libjoint`, `libislet`, `liblemma`). |

### Testing, Release Distribution & Operations
| Document | Focus |
| :--- | :--- |
| **[`docs/TESTING.md`](./docs/TESTING.md)** | Unit, integration, and Playwright end-to-end testing requirements. |
| **[`docs/CI_NPM_DISTRIBUTION.md`](./docs/CI_NPM_DISTRIBUTION.md)** | CI automation and universal C/WASM package distribution via `@tty-pt/*` on NPM. |
| **[`docs/RELEASE_EXECUTION_STRATEGY.md`](./docs/RELEASE_EXECUTION_STRATEGY.md)** | Ecosystem release sequencing and topological publishing strategy across submodules. |
| **[`docs/EXTENSIONS.md`](./docs/EXTENSIONS.md)** | Pi coding agent extension workflows and custom agent commands. |
| **[`docs/ADVANCED_GIT_RECOVERY.md`](./docs/ADVANCED_GIT_RECOVERY.md)** | Git recovery workflows, reflog reconstruction, and submodule state preservation. |
| **[`docs/archive/`](./docs/archive/)** | Completed architecture journals (`ST.md`, `NO_WIZ.md`), past release wave records, and historical audits. |

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
- **`/i18n`** — Internationalization dictionary registry and locale string resolution.
- **`/source`** — Dataset registration and HTTP API transport over `libhyle-source`.
- **`/index`** — Generic CRUD routes, list views, and picker option endpoints.
- **`/poem`** — Poem upload, display, and listing.
- **`/song`** — Song detail, chord charts, transposition, and media attachments.
- **`/grp`** — Group repertoire management and format categories.
- **`/gig`** — Gigs and setlists with transpose settings and format tags.
- **`/software`** — Software catalog and Markdown project documentation viewer.
- **`/mpfd`** — Streaming multipart/form-data parser.
- **`/redir`** — Legacy route redirection helpers.
- **`/site_chrome`** — WASM client chrome enhancement (`htdocs/site_chrome.wasm`).
- **`/auren`** — Local in-browser WebAssembly LLM inference sandbox (`mods/auren/ux/` $\rightarrow$ `auren.so` & `auren.wasm`).
- **`/chat`** — Multi-session local LLM streaming chat workspace (`mods/chat/ux/` $\rightarrow$ `chat.so` & `chat.wasm`) backed by `axil-auren`.
- **`/nd`** — Isomorphic frontend reactor for NeverDark MUCK (`mods/nd/ux/` $\rightarrow$ `nd.so` & `nd.wasm`) with pluggable TTY, Lamina 2D, and Seven 2.5D/3D toroidal planetary WebGL viewports.

### Core Infrastructure Submodules (`external/`)
- **`axil`** — Single-port HTTP/WebSocket network daemon and transport multiplexer (`@tty-pt/axil`).
- **`axil-auth`** — Authentication and session token management module for `axil`.
- **`axil-hyle`** — Framework-neutral `axil`↔`hyle` REST CRUD connector.
- **`axil-tty`** — WebSocket-to-PTY interactive browser terminal bridge (`@tty-pt/axil-tty`).
- **`axil-auren`** — Pure-C local LLM inference, embedding, and classification server.
- **`axil-resume-search`** — Pure-C PDF resume ingestion, vector embedding, and job candidate matching service.
- **`libxylem`** — Zero-cost C function dispatch bus and region-isolated module loader (`@tty-pt/xylem`).
- **`libcorm`** — In-memory key-value map store and multi-axis recall kernel (`@tty-pt/corm`).
- **`libjoint`** — Temporal presence interval-tree engine (`@tty-pt/joint`, Time axis).
- **`libislet`** — Multi-dimensional Morton code (Z-order) spatial index (`@tty-pt/islet`, Space axis).
- **`liblemma`** — Accent-sensitive lexical tokenizer and inverted FTS index (`@tty-pt/lemma`, Text axis).
- **`libsepal`** — Two-stage ANN semantic vector store (`@tty-pt/sepal`, Meaning axis).
- **`libbud`** — Pure C virtual DOM tree builder, 5-field state binder, and VDOM diff engine (`@tty-pt/bud`).
- **`libhyle`** — Relational schema engine, query planner, and full-text search index (`@tty-pt/hyle`).
- **`libhyle-source`** — Pluggable dataset persistence engine (`store_fs`, `store_mem`, DSV, JSON overlays).
- **`libhyle-bud`** — Bridge connecting Hyle schema hints to Bud UI binders (`@tty-pt/hyle-bud`).
- **`libhyle-react`** — React/Next.js bindings for Hyle schemas and Bud state reconciliation.
- **`libstele`** — Universal system substrate (`<ttypt/stele.h>`, `<ttypt/iso.h>`) and single shared WebAssembly allocator (`@tty-pt/stele`).
- **`libchaff`** & **`libareole`** — Pure-integer multi-scale recursive 1/f noise (`@tty-pt/chaff`), toroidal solar/climate geometry, and 16-level quadtree spatial LOD & frustum culling hierarchy (`@tty-pt/areole`).
- **`liblamina`** — 2D software tile blitter and 2.5D/3D WebGL continuous heightfield & Beer-Lambert water optics bridge (`@tty-pt/lamina`).
- **`libpitch`** — Pure-C musical chord parser and key transposition library (`@tty-pt/pitch`).
- **`axil-nd`** — NeverDark MUCK server engine with 4D spacetime universe simulation.
- **`nd-*`** — Gameplay and content tenant modules (`nd-attr`, `nd-biome`, `nd-class`, `nd-drink`, `nd-equip`, `nd-fight`, `nd-level`, `nd-mob`, `nd-mortal`, `nd-other`, `nd-plant`, `nd-race`, `nd-region`, `nd-seat`, `nd-shop`, `nd-spell`, `nd-stone`, `nd-vanilla`, `nd-weather`, `nd-world`, `nd-wts`).
