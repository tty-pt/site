# Changelog

All notable changes to the site and NeverDark platform will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased] (target: v1.3.0)

- **NeverDark 2.5D Seven Continuous Toroidal Planetary & Terrain Engine**:
  - Implemented continuous 3D toroidal geometry ($R=2,000,000$, $r=1,335,000$) with periodic coordinate wrapping, analytical normals, tangent frames, and metric positivity across world boundaries without topological seams.
  - Geometrically accurate **toroidal solar incidence & climate integration** (`external/nd-world/include/nd/field.h`, `external/libnoise/include/ttypt/chaff.h`): replaced flat Y-boundary polar temperature assumptions with 3D torus surface normals, inner-ring hole illumination and outer-ring self-shadowing (`R - r cos(theta)`), planetary rotation/azimuth, and continuous X/Y periodic wrapping integrated into the biome and flora climate system (`nd-biome` alpine meadow, mediterranean, and wetland; `nd-plant` 16 flora species and coastal water transitions).
  - Multi-scale continuous 1/f procedural noise engine (`external/libnoise` / `libchaff`): continental mega-octaves, intermediate ridged mountain octaves (`shifts 6..9`), Mount Everest benchmark alpine peak (+1854 units), abyssal trenches (-2000 units), altitude lapse rate, soil/sand micro-materials, and continuous $C^2$ intra-room procedural elevation relief ($|H_{\text{fine}} - H_{\text{coarse}}| \le 63$).
  - Physical water optics: Beer-Lambert volumetric absorption ($I(d) = I_0 e^{-\alpha d}$), shallow seabed visibility with turquoise absorption gradients, shoreline foam swash, dynamic wave perturbation, and zero-arithmetic water depth calculation.
  - 2D/3D WebGL rendering pipeline (`external/liblamina/htdocs/lamina-bridge.js`, `mods/nd/ux/nd_seven_ground.c`): branchless bilinear heightfield reconstruction, analytical surface normals, `uSeaLevel` fragment shader uniform support (`FS_TERRAIN_3D`), and linear substrate texture filtering.
  - Organic 2D dispersion and natural object placement: blue-noise spacing, expanded slot domains (slot 0: 72, slot 1: 44–116, slot 2: 98–138), arrival clear box flank occupancy, and deterministic grove/outcrop placement.
- **Multi-Scale Spatial Hierarchy Engine (`external/libchunk`)**:
  - 16-level quadtree LOD cascade, analytic unprojection, frustum culling, priority queue refinement, and guaranteed 4-quadrant continuous coverage across extreme camera sweeps (4.0x down to 0.00001x).
  - GLSL shader parity headers (`ttypt/chunk_glsl.h`, `ttypt/chaff_glsl.h`) for identical client-server procedural evaluation.
- **Dynamic Viewport Driver Registry & HUD Lifecycle**:
  - Dynamic runtime switching between ANSI TTY terminal mode and Lamina Seven 2.5D graphical mode via action dock (`mods/nd/ux/nd_ui.c`, `nd_dock_ui.c`).
  - Self-healing Web HAL bridge remounting with write buffering during DOM patch replay (`htdocs/stele-client.js`, `htdocs/axil-tty-bridge.js`, `external/libbud`).
  - RPG HUD: collapsible chat console (`#rpg-chat`), room header, and live entity status.
- **Local In-Browser LLM Inference Engine (`axil-auren` & `mods/auren`)**:
  - Integrated in-browser WebAssembly LLM inference via Wllama / Qwen / Llama models.
  - Dedicated UI frontend (`mods/auren/ux/auren_fe.c`, `auren_ui.c`) and styles.
- **Hyle Data Engine & Bud Binary Streaming (`HYLT`)**:
  - High-throughput binary list serialization protocol (`HYLT` format, `mods/common/ux/list_bin.c`, `tests/pages/26-binary-table-streaming.sh`).
  - Interactive multi-column table sorting (`mods/index/ux/list_sort.c`, `tests/pages/27-column-sorting.sh`).
  - In-memory caching and fragment streaming (`external/libhyle-bud`, `htdocs/hyle-fragments.js`).
  - Sovereign design system stylesheet (`mods/common/htdocs/styles.css`).
  - React/Next.js binding library (`external/libhyle-react`).
- **Developer Experience & Tooling Hardening (`.pi/extensions/pi-quest` & `.pi/extensions/pi-mm`)**:
  - `pi-quest` (unreleased, target v1.12.0): implemented scratch workspace leasing pool (`.pi/scratch/slot-*`), git worktree isolation, unified `/quest` operator command surface (consolidating `garden` into `/quest list` & `/quest del`, default disabled mode, zip-less archive), `@tty-pt` worktree symlink isolation, `core.worktree` restoration, empty-tree commit guards, and automatic `git submodule update` alignment after merge/squash/sanitization.
  - `pi-mm` (unreleased, target v0.7.0): implemented multi-axis codebase mipmap graph & lore fusion (`scope: "code"`, replacing `pi-cbm`), ephemeral semantic session memory & mipmap compaction (`scope: "session"`, replacing `pi-vcc`), L0 consolidation backlog unwedging, zero-config local synthesis, and unified `mm-judge` status bar telemetry.
- **Synchronized Unreleased Submodule Updates**:
  - `axil` (target v1.8.0): packaged `@tty-pt/axil` with AxilClient, Web HAL bridge, and libstele integration in `libaxil.c`.
  - `axil-hyle` (target v1.1.2): updated packaging dependencies to explicitly include libstele.
  - `axil-nd` (target v1.4.0): introduced BCP macro view and sparsity packets (`include/nd/bcp-meta.h`, `include/nd/bcp-types.h`), water map flag, and fallback UI scripts.
  - `axil-tty` (target v1.6.0): standardized Web HAL bridge and TerminalController under `@tty-pt/axil-tty` (`cjs/axil-tty.js`), enhanced unmount detachment and remount with write buffering (`htdocs/axil-tty-bridge.js`), and tracked generated web assets.
  - `libbud` (target v1.3.0): client-side hydration engine (`htdocs/bud-hydrate.js`), state serialization enhancements (`include/bud/bud_state.h`), and batched capability remounting on patch replay boundaries (`src/libbud.c`).
  - `libhyle` (target v1.7.0): sovereign design system stylesheet and styling assets (`htdocs/hyle.css`).
  - `libhyle-bud` (target v1.2.0): fragment engine (`htdocs/hyle-fragments.js`), in-memory cache (`src/hyle-bud-cache.c`), and form/table/filter enhancements (`src/filter.c`, `src/form.c`, `src/table.c`).
  - `libhyle-source` (target v1.4.0): packaged `@tty-pt/hyle-source`, implemented optimistic cache (`src/cache.c`), browser bundling, N-API native bindings (`npm/native/hyle_source_napi.c`), WebAssembly compilation (`npm/wasm/hyle_source_wasm.c`), and picker/store interfaces.
  - `libislet` (target v1.4.0): implemented `islet_box_is_empty` Morton box spatial query test (`include/ttypt/islet.h`, `src/libislet.c`).
  - `libstele` (target v1.6.0): zero-reload SPA navigation, warm-memory slot swapping, event bus (`src/stele_event.c`, `src/stele_wasm_kernel.c`), and active capability bridge synchronization with DOM patch remounting (`htdocs/stele-client.js`).
  - `libxylem` (target v1.8.0): standardized Web HAL capability bridge under `@tty-pt/xylem` (`npm/dist/index.js`, `wasm-allowed-imports.lst`).
  - `mk` (target v0.4.0): centralized WASM toolchain, BSD make optimizations, local `add-prefix-dirs` and `npm_inc` priority before system `PREFIX`, worktree support, and wasm export auditing (`scripts/wasm-allowed-exports.lst`, `scripts/wasm-audit.sh`).
  - 19 NeverDark Tenant Modules (`nd-attr`, `nd-biome`, `nd-class`, `nd-core`, `nd-drink`, `nd-equip`, `nd-fight`, `nd-level`, `nd-mob`, `nd-mortal`, `nd-other`, `nd-plant`, `nd-race`, `nd-seat`, `nd-shop`, `nd-spell`, `nd-stone`, `nd-vanilla`, `nd-wts`): updated `stllog` comments, removed `add-prefix` paths, cleaned module dependencies, expanded `nd-biome` (`alpine meadow`, `mediterranean`, `wetland`), and expanded `nd-plant` (16 species + coastal water transition).

## [1.2.1] - 2026-11-04

- **Release Wave 2026.12.1 — Universal Substrate & Package Ecosystem Synchronization**:
  - Published and verified ecosystem packages under canonical `@tty-pt/*` scope across NPM and native OS package distributions (`@tty-pt/stele@1.5.2`, `@tty-pt/bud@1.2.2`, `@tty-pt/corm@2.2.4`, `@tty-pt/joint@1.6.3`, `@tty-pt/islet@1.3.2`, `@tty-pt/lemma@1.2.2`, `@tty-pt/sepal@1.3.2`, `@tty-pt/xylem@1.7.2`, `@tty-pt/hyle@1.6.1`).
  - Aligned `@tty-pt/lamina` and `@tty-pt/transp` versions to `0.1.0` and package definitions.
  - Migrated `libhyle-source` (`1.3.1`), `seven-ways`, `neverdark`, and `axil-auren` to link and depend on `libstele` rather than legacy `libqsys`.
  - Enforced pkg-config version parity validation in local `make boundary-check` (`scripts/check-pkgconfig-versions.sh`) and CI test phase.

## [1.2.0] - 2026-11-02

- **Universal WebAssembly Substrate & Pure Isomorphic UX Architecture (`libstele` & `ISO_EXPORT`)**:
  - Renamed `libqsys` to `libstele` across submodules (`external/libstele`), C headers (`<ttypt/stele.h>`, `<ttypt/stele_wasm.h>`), kernel symbols (`stele_kernel_*`), pkg-config (`stele.pc`), and npm packages (`@tty-pt/stele`), retaining full backward compatibility shims.
  - Canonized `ISO_EXPORT`, `ISO_EXPORT_AS`, and `ISO_IMPORT` in `<ttypt/iso.h>` as the canonical isomorphic export contract across native shared objects (SSR) and WebAssembly reactors (browser), deprecating `WASM_EXPORT` aliases.
  - Eliminated brittle relative filesystem traversal hacks (`../../libstele/npm/...`) across all ecosystem libraries (`@tty-pt/islet`, `@tty-pt/corm`, `@tty-pt/joint`, `@tty-pt/lemma`, `@tty-pt/sepal`, `@tty-pt/hyle`), resolving substrate through canonical package imports (`@tty-pt/stele`) and `node_modules/@tty-pt/*` monorepo links.
  - Restored `external/libbud` to pure C UI tree building and VDOM diffing (`bud_vdom_diff`), eliminating runtime slot mounting, host patch imports, and `#if defined(__wasm__)` preprocessor guards.
  - Relocated application-level WASM slot orchestration, patch emission, and DOM event dispatching to `mods/common/site_wasm_app.c` and `mods/common/site_wasm_app.h`.
  - Implemented dynamic on-demand component WASM instantiation with isolated linear memory instances per component in `htdocs/stele-client.js`, eliminating allocator heap collisions at fixed base offsets (`--global-base=1024`).
  - Purged all clever `#ifdef __wasm__` / `#ifndef __wasm__` preprocessor branching from feature C code in `mods/*/ux/*.c` and schema definitions in `fields.h`.
  - Stabilized and verified all 123 E2E and integration tests (`make test`), including song key transposition, gig zoom, and browser vim movement.

## [1.1.0] - 2026-10-25

- **Ecosystem Wide Synchronized Release Wave 2026.11**:
  - Published and verified 18 interconnected libraries and platform services across 10 native packaging matrices (Debian/Ubuntu, Alpine, Rocky Linux, Arch Linux, MSYS2/MinGW, macOS Intel, macOS Apple Silicon, Windows Winget, and OpenBSD):
    * **System & Foundations**: `libqsys` (v1.3.1), `libcorm` (v2.0.1), `libbud` (v1.0.2), `libxylem` (v1.6.0).
    * **Corm Recall Axes**: `libjoint` (v1.5.0, time), `libislet` (v1.2.1, space), `libsepal` (v1.2.0, semantics), `liblemma` (v1.1.0, lexics).
    * **Servers & Data Kernel**: `axil` (v1.6.0), `libhyle` (v1.5.0), `libhyle-source` (v1.2.0), `libhyle-bud` (v1.0.2).
    * **Harness Bridges & LLM Server**: `axil-auth` (v1.2.1), `axil-tty` (v1.4.1), `axil-hyle` (v1.0.2), `axil-auren` (v1.2.0).
    * **Developer Tools & Journaling Extensions**: `pi-quest` (v1.2.0), `pi-mm` (v0.3.1).
- **Submodule URL Normalization & Repository Canonicalization (`.gitmodules`)**:
  - Replaced legacy redirected URLs (`libqmap.git` → `libcorm.git`, `libstoma.git` → `liblemma.git`, `libgeo.git` → `libislet.git`, `libit.git` → `libjoint.git`, `hyle.git` → `libhyle.git`).
  - Standardized `.git` URI naming across all 16 NeverDark tenant module submodules.
- **Ecosystem Release Execution Strategy (`docs/RELEASE_EXECUTION_STRATEGY.md`)**:
  - Documented strict topological DAG layers, bounded concurrency batching rules (3–4 at a time), pre-flight quality gates, Windows/MinGW header hygiene, and CI remote lock troubleshooting.

## [1.0.0] - 2026-10-24

- **Turnkey Full-Universe Deployment (`./start.sh`, `scripts/seed-nd-universe.sh`, `tests/e2e/test_full_universe.sh`)**:
  - Unified single-port (`:8080`), single-process architecture serving SSR HTML, static WASM bundles, full-duplex WebSocket streams, and raw TCP Telnet CLI on one endpoint.
  - Automated universe seeder (`scripts/seed-nd-universe.sh`) generating 32-planet star sectors with pre-configured citadel hubs, starter equipment, and NPC merchants.
  - Headless full-universe integration test harness (`tests/e2e/test_full_universe.sh`) and browser post-seed E2E suite (`tests/e2e/nd-browser-post-seed.test.ts`).
- **Pure-C Isomorphic Bud UI for NeverDark (`mods/nd/ux/nd_ui.c`, `htdocs/nd.wasm`)**:
  - Full-fidelity SSR-to-WASM state hydration through embedded `<script id="bud-state">` JSON and instant WASM initialization.
  - Multi-viewport rendering engine supporting simultaneous 3D hardware-accelerated WebGL (`mods/nd/ux/nd_webgl.c`), 2D Lamina canvas (`mods/nd/ux/nd_lamina.c`), pure-C Bud DOM, and ANSI TTY terminal.
  - Interactive RPG HUD components: 2-column character stats sheet, HP/MP vitality bars, 4x5 paper doll equipment modal, and grimoire spellbook overlay.
  - Planet 2 (Aethelgard) showcase featuring perspective 3D room boxes, cutaway exit archways, stylized 3D avatar prisms, target halos, and dynamic Time-of-Day sun/sky lighting.
- **Web HAL 60fps Frame Loop Contract (`wasm_tick`)**:
  - Implemented `wasm_tick` continuous rendering contract across `libbud`, `htdocs/bud-client.js`, and `mods/nd/ux/nd_ui.c`.
  - Dynamic viewport invalidation and delta animation frames driving the 3D WebGL renderer and 2D canvas without DOM thrashing.
- **Universal `bud_slot` & `bud_state` Keyed Hydration**:
  - Standardized `BUD_SLOT_DEFINE` and tenant hook contributions (`BUD_SLOT_CONTRIBUTE`) across NeverDark modules (`nd-attr`, `nd-equip`, `nd-fight`, `nd-mortal`, `nd-spell`), song detail (`mods/song/ux/detail.c`), and site shell (`mods/site_chrome/ux/chrome.c`).
  - Clean pre-link boundary verification and strict module encapsulation.
- **In-WASM Xylem Hook Dispatcher (`libxylem-wasm.c`)**:
  - Freestanding pure-C in-WASM hook registry and listener chaining (`XY_LISTENER_TAG`, `xy_call`, `xy_last`) enabling cross-module event dispatching inside the browser without host OS `dlopen`.
- **Declarative Zero-Arithmetic BCP Protocol & Axil WebSocket Multiplexing**:
  - High-throughput binary framing (`#b` magic) and WebSocket channel multiplexing: Stream 0 (Text/TTY), Stream 1 (Binary BCP), Stream 2 (JSON/RPC).
  - Zero-arithmetic packed memory-mapped structs with X-macro schema descriptors in `nd/bcp-meta.h`.
- **Build Portability, Linker Purity & DT_RUNPATH Elimination**:
  - Complete elimination of `DT_RUNPATH` / `RPATH` and in-tree `LD_LIBRARY_PATH` exports, linking cleanly against system authority (`/usr/lib`).
  - Decoupled 19 independent tenant domain modules on the XY bus, enforcing strict two-header separation (`<nd/*-types.h>` and `<nd/*.h>`) and eliminating all `*_IMPL` guards.
  - Zero site-specific JavaScript: generic client bus (`htdocs/axil-bridge.js`, `htdocs/bud-client.js`, `htdocs/bud-hydrate.js`) with all application logic residing in pure C and compiled WASM.
- **Unified Reusable Isomorphic Engine (`external/mk/iso.mk`, `<ttypt/iso.h>`)**:
  - Complete elimination of `-Wl,--export-all` across all WebAssembly binaries via reusable `external/mk/iso.mk`.
  - Standardized universal `ISO_EXPORT`, `ISO_EXPORT_AS`, and `ISO_IMPORT` macros in `<ttypt/iso.h>`, eliminating all `#ifdef __wasm__` branching from feature UX code.
  - Enabled `-fvisibility=hidden` and `-Wl,--gc-sections` dead-code stripping, shrinking client WASM binaries by 30-40% and bounding export tables to 17-23 intentional symbols.
  - Added integrated export allowlist audit (`external/mk/scripts/wasm-audit.sh`) wired into `make boundary-check`.
- **Web HAL Specification & Comprehensive Release Wave Documentation**:
  - Published master Web Hardware Abstraction Layer specification (`docs/WEB_HAL_SPECIFICATION.md`) and implementation roadmap (`docs/WEB_HAL_IMPLEMENTATION_PLAN.md`).
  - Standardized documentation, READMEs, and architectural reviews across the 39-target ecosystem (`docs/RELEASES_PLAN.md`).
