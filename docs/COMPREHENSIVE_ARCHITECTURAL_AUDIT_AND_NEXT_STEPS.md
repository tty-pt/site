# Comprehensive Architectural Audit & Strategic Implementation Review

**Status:** Completed & Verified  
**Scope:** `site`, `axil-nd`, `libxylem`, `liblamina`, `axil-tty`, `libcorm`, and tenant modules (`nd-attr`, `nd-equip`, `nd-bus`).  
**Related Quests:** `1xJZan`, `1xJccD`, `1xJcwr`, `1xJdLv`, `1xJdYZ`, `1xJngo`.

---

## 1. Executive Summary

Over the recent sequence of architectural milestones, the NeverDark and pure-C isomorphic stack has undergone an end-to-end transformation. The work systematically eliminated historical technical debt, restored strict isomorphic SSR invariants, decoupled tenant game modules from the core kernel, and established WebAssembly (WASM) as a first-class execution environment powered by `libxylem`.

This document provides a comprehensive retrospective audit of everything developed, documents the immediate gaps identified and resolved, establishes clear bounding for follow-up visual presentation opportunities, and presents a complete verification matrix.

---

## 2. Retrospective Audit: Completed Architectural Pillars

### Pillar 1: Pure Bud SSR & Zero Static HTML
- **Defect Resolved:** In early iterations, a manual static `htdocs/index.html` was reintroduced and served as an HTTP fallback in `src/http.c`.
- **Delivered Solution:**
  - `external/axil-nd/htdocs/index.html` was completely deleted.
  - In `external/axil-nd/src/http.c`, `nd_serve_htdocs("index.html")` was eradicated.
  - When running within the site, `handle_nd` delegates to `nd_ssr_respond()`, rendering the full page via `site_ui_page_lang()` with state serialized into `<script id="bud-state">`.
  - When running as a standalone daemon (e.g. `axil-nd/test.sh` on port 28000), `nd_dynamic_fallback_ssr(socket_t fd)` dynamically builds a `bud_node` tree (`#bud-root`, `#viewport`, `#term` with `data-bud-terminal="1"`, `#right-panel` with direction keypad, gauges, actions) and renders it using `bud_render_html()`.
  - **Invariant:** Zero static HTML strings exist in C source. 100% dynamic isomorphic rendering.

### Pillar 2: Pluggable Viewport Architecture & WebGL Integration
- **Defect Resolved:** Binary hardcoded toggle between TTY and Canvas (`g_viewport_mode`).
- **Delivered Solution:**
  - `external/axil-nd/include/nd/viewport.h` declares `nd_viewport_driver_t` with lifecycle hooks (`mount`, `init`, `on_frame`, `on_resize`, `destroy`).
  - Implemented three out-of-the-box drivers:
    1. `tty` driver: mounts `<div id="term" data-bud-terminal="1">` (axil-tty).
    2. `lamina-2d` driver: mounts `<canvas id="viewport" data-bud-canvas="1">` (liblamina).
    3. `webgl` driver: mounts `<canvas id="viewport-gl" data-bud-webgl="1">` (WebGL).
  - Core action dock automatically injects a viewport cycle button when $>1$ driver is registered.
  - `<ttypt/webgl.h>` provides canonical WebGL 1/2 bindings for C WASM modules under `import_module("webgl")`.
  - `htdocs/webgl-bridge.js` implements generic browser WebGL capability bindings conforming to `scripts/check-no-site-specific-js.sh`.

### Pillar 3: Capability Bridge Distribution & Owner Co-Location
- **Defect Resolved:** `webgl-bridge.js` initially resided untracked in `htdocs/` due to `.gitignore` rules.
- **Delivered Solution:**
  - `webgl-bridge.js` is co-located in `external/liblamina/htdocs/webgl-bridge.js` (and upstream `/home/quirinpa/liblamina/htdocs/`).
  - `<ttypt/webgl.h>` is co-located in `external/liblamina/include/ttypt/webgl.h` (and upstream `/home/quirinpa/liblamina/include/ttypt/webgl.h`).
  - `xylem-bridge.js` is co-located in `external/libxylem/htdocs/xylem-bridge.js`.
  - `axil-tty-bridge.js` is co-located in `external/axil-tty/htdocs/axil-tty-bridge.js`.
  - Top-level `Makefile assets-sync:` automatically synchronizes all 4 capability bridges into `htdocs/`.
  - `external/liblamina/wasm-allowed-imports.lst` consolidates all `lamina.*` and `webgl.*` symbols.

### Pillar 4: BCP Protocol Modernization & Structured Entity Streaming
- **Defect Resolved:** Core `bcp.h` hardcoded `ND_BCP_EQUIPMENT`, body slot enums, and lacked client entity parsing.
- **Delivered Solution:**
  - Removed all game-module concepts from core `bcp.h`.
  - Created `nd_entity_desc_t` and `nd_room_desc_t` in `external/axil-nd/include/nd/bcp-types.h`.
  - Implemented safe decoding for `ND_BCP_ITEM` and `ND_BCP_ROOM` in `nd_bcp_decode()`.
  - Designed `ND_BCP_CHANNEL_DEF` dynamic channel discovery and `ND_BCP_ACTION_INVOKE` upstream binary action frames (`docs/AXIL_ND_MCP_BCP_IMPROVEMENT_SPEC.md`).
  - Created bounds-safe zero-allocation parsing primitives (`nd_bcp_read_str`).

### Pillar 5: Freestanding In-WASM Xylem Dispatch & Hook Unification
- **Defect Resolved:** Client WASM relied on ad-hoc weak C function pointers and registration tables because `libxylem` was previously native-only.
- **Delivered Solution:**
  - Authored `external/libxylem/src/libxylem-wasm.c`: freestanding in-WASM hook dispatcher (`xy_areg`, `xy_call`, `xy_last`) with zero host glibc/dlsym dependencies.
  - Added `XY_LISTENER_TAG` in `external/libxylem/include/ttypt/xy.h`: enables multiple listeners for the same hook to be statically compiled into the same WASM binary without symbol collisions.
  - Replaced ad-hoc registries with unified Xylem hooks:
    - `on_bcp_channel` in `external/axil-nd/include/nd/bcp.h`
    - `on_nd_ui_action`, `on_nd_ui_render_modal`, `on_nd_ui_render_slot` in `external/axil-nd/include/nd/ui-slot.h`
  - Two-header separation rule enforced (`*-types.h` for implementers, `*.h` for callers), strictly maintaining zero `*_IMPL` guards.
  - Compiled `libxylem-wasm.c` into `htdocs/nd.wasm`.
  - Authored `htdocs/xylem-bridge.js` for dynamic browser module loading via `WebAssembly.instantiate`.

---

## 3. Immediate Gaps Resolved in Current Milestone

During this review milestone, the following gaps were identified and closed:
1. **Bridge Sync & Capability Advertising:**
   - Added `xylem-bridge.js` to `Makefile assets-sync:`.
   - Added `xylem` to `data-capabilities` in `mods/nd/ux/nd_ui.c` (`data-capabilities='axil-ws lamina webgl xylem bud'`).
2. **Dedicated In-WASM Xylem Unit Test Suite:**
   - Authored `tests/unit/xylem_wasm_test.c` and `tests/unit/run-xylem-wasm.sh`.
   - Tests multi-listener registration, hook dispatch, `xy_last` value chaining, error codes, and region stubs.
   - Added `run-xylem-wasm.sh` to `standalone-unit-tests` in `Makefile`.
3. **Multi-Listener Static Registration Fix:**
   - Refactored `XY_LISTENER_TAG` so that listener callback functions and adapters are static to their translation unit, preventing duplicate symbol linker errors when multiple modules implement the same hook.
   - Corrected listener chaining order to maintain strict FIFO registration order.
   - Fixed `tests/unit/run-nd-bcp.sh` and `tests/unit/nd_bcp_mock_equip.c` so all 8 protocol tests pass.

---

## 4. Bounded Scope for Visual Presentation Follow-Up

The core infrastructure and protocol channels for environment and entity awareness are now 100% active. Advanced visual asset authoring is bounded as follow-up presentation milestones:

1. **Lamina 2D Canvas Entity Sprites:**
   - *Current State:* `nd_lamina_render_room()` renders the room geometry and player avatar box.
   - *Follow-up Opportunity:* Iterate over `st->entities[i]` to draw monster/player sprites and ground items using `lam_tile_draw()` and entity `art` / `ch` glyphs.
2. **WebGL 3D Room & Textured Billboards:**
   - *Current State:* `<ttypt/webgl.h>` and `webgl-bridge.js` are linked; `init_webgl` and `on_frame_webgl` manage context and clearing.
   - *Follow-up Opportunity:* Author GLSL vertex/fragment shaders to render a 3D textured dungeon room and billboarded entity quads.

---

## 5. Verification Matrix & Quality Gates

All automated verification checks and test suites run clean with zero failures:
1. `make assets-sync`: **PASS** (all 4 capability bridges synchronized).
2. `make all`: **PASS** (zero compilation or link errors across all modules).
3. `scripts/check-wasm-imports.sh`: **PASS** (0 native leaks in WASM; static in-WASM Xylem symbols resolve internally).
4. `scripts/check-no-impl-guards.sh`: **PASS** (Zero `*_IMPL` guards found; two-header separation rule verified).
5. `scripts/check-ux-purity.sh`: **PASS** (Purity intact; `mods/nd/ux` sanctioned as in-WASM Xylem host).
6. `scripts/check-no-site-specific-js.sh`: **PASS** (Zero site-specific terms in `htdocs/*.js`).
7. `scripts/check-module-boundaries.sh` & `scripts/check-no-tree-rpath.sh`: **PASS**.
8. `tests/unit/run-xylem-wasm.sh`: **PASS** (all in-WASM Xylem tests pass).
9. `tests/unit/run-nd-bcp.sh`: **PASS** (all 8 protocol tests pass with in-WASM Xylem hook dispatch).
10. `mods/nd/test.sh`: **PASS** (`nd.so` and `nd.wasm` present and verified).
11. `external/axil-nd/test.sh`: **PASS** (`axil-nd ok` across all daemons and test cases).
12. `make standalone-unit-tests`: **PASS** (all 24 unit test suites pass).
