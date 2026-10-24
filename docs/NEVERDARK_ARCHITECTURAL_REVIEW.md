# Comprehensive Architectural Review: NeverDark & Pure-C Isomorphic Stack

**Scope:** Review of all git diffs, working copy modifications, and commit history across `site`, `external/axil-nd`, `external/nd-attr`, and `external/nd-equip` since the last release.

---

## 1. Multi-Repository Git Diff & Commit Inspection

A systematic audit of the commit history and working tree diffs across the four affected repositories reveals the progression from the initial flawed integration to the decoupled architecture:

### A. Repository `site` (`/home/quirinpa/site`)
- **Git Commit Progression (`bf0ebf2` .. `14da0fa`):**
  - Commit `bf0ebf2`: Added `mods/nd`, integrating NeverDark into `mods.load` and building `nd.so` + `nd.wasm`.
  - Commits `ccd5692` & `b37500c`: Separated capability bridge scripts (`axil-tty-bridge.js`, `lamina-bridge.js`) and two-level namespaces (`"axil"`, `"tty"`, `"lamina"`).
  - Commit `5780c0d`: Refactored bridge orchestrator and resolved linker symbols.
  - *Shortcuts in early commits:* Monolithic `mods/nd/ux/nd_ui.c` (422 lines) hardcoded the equipment action button (`👕`), character sheet (`📋`), and binary viewport toggle (`ND_VIEWPORT_TTY` vs `ND_VIEWPORT_CANVAS`).
  - *Regressions:* `bud-client.js` broke legacy WASM modules expecting `env` imports (`TypeError: Import #0 "bud": module is not an object or function`).
- **Working Tree Diff (Decoupling Architecture):**
  - `mods/nd/ux/nd_ui.c` & `mods/nd/ux/nd_ui.h`: Stripped 100% of equipment code and hardcoded modal renderers. Replaced with dynamic UI slot loops over `ND_UI_SLOT_ACTIONS`, `ND_UI_SLOT_MODALS`, `ND_UI_SLOT_GAUGES`, and `ND_UI_SLOT_VIEWPORT`.
  - `mods/nd/ux/nd_attr_ui.c`: Extracted character attributes modal, HP/MP gauges, and `📋` button as a modular client extension.
  - `mods/nd/ux/nd_equip_ui.c`: Extracted equipment doll modal, `👕` action button, and BCP unpacker as a modular client extension.
  - `include/ttypt/webgl.h`: Added canonical WebGL 1/2 bindings for C WASM modules under `import_module("webgl")`.
  - `include/ttypt/webgl-wasm-allowed-imports.lst`: Added allowlist for WebGL WASM imports.
  - `htdocs/bud-client.js`: Refactored to inspect `WebAssembly.Module.imports(wasmModule)` and synthesize `imports.bud`, `imports.env`, and declared capability namespaces dynamically, guaranteeing 100% backward compatibility for older modules.
  - `tests/unit/nd_bcp_test.c`: Added test coverage verifying dynamic sub-channel registration and dispatch.

### B. Repository `external/axil-nd`
- **Git Commit Progression (`fc126db` .. `8fc6bb9`):**
  - Commit `e1befff`: Added `include/nd/bcp.h` and SSR route handler.
  - Commit `8fc6bb9`: Added `data-capabilities` attributes.
  - *Shortcuts in early commits:* Created `htdocs/index.html` (manual static HTML with hardcoded DOM nodes). Hardcoded `ND_BCP_EQUIPMENT = 12`, `enum nd_bodypart_idx`, and `nd_equipment_state_t` directly into `include/nd/bcp.h`. In `src/http.c`, served `index.html` as a fallback when `nd_ssr_respond` returned non-zero.
- **Working Tree Diff (Decoupling Architecture):**
  - `htdocs/index.html`: Completely removed (`deleted: htdocs/index.html`).
  - `include/nd/bcp.h`: Stripped `ND_BCP_EQUIPMENT`, `nd_equipment_state_t`, and body slot enums. Added `nd_bcp_unpack_fn`, `nd_bcp_register_channel()`, and dynamic opcode dispatch.
  - `include/nd/ui-slot.h`: New header defining `nd_ui_slot_t`, `nd_ui_action_button_t`, and slot registration APIs.
  - `include/nd/viewport.h`: New header defining `nd_viewport_driver_t` and viewport registry APIs.
  - `src/http.c`: Eliminated `nd_serve_htdocs("index.html")`. Updated `handle_nd` so SSR via `nd_ssr_respond` is the primary entrypoint, with dynamic fallback SSR (`nd_fallback_ssr_respond`) when running standalone without site modules.

### C. Tenant Repositories `external/nd-attr` & `external/nd-equip`
- `external/nd-attr`:
  - Added `include/nd/attr-ui.h`, encapsulating the character attributes modal renderer, stats action button, and HP/MP gauges renderer.
- `external/nd-equip`:
  - Added `include/nd/equip-bcp.h`, defining `ND_BCP_EQUIPMENT = 12`, `enum nd_bodypart_idx`, `nd_equipment_state_t`, and channel unpacker `nd_equip_bcp_unpack()`.
  - Added `include/nd/equip-ui.h`, encapsulating the equipment doll modal renderer and action button (`👕`).

---

## 2. Architectural Evaluation Against Core Invariants

### A. SSR Contract & Single-Source-of-Truth
- **Site SSR (`/nd`):** Strict compliance achieved. `mods/nd/nd.c` renders the initial state into the DOM using `nd_ui_render(&st)` and wraps it with `site_ui_page_lang()` and `<script id="bud-state">`. The client WASM reactor hydrates from this exact state.
- **Standalone `axil-nd` Fallback:** In `external/axil-nd/src/http.c`, when `nd_ssr_respond` is unhooked, the engine serves `nd_fallback_ssr_html[]`.
  - *Evaluation:* This eliminated the static file on disk and preserved zero-dependency micro-kernel operation for `axil-nd` test runs (`./test.sh`).
  - *Technical Debt:* The fallback is currently an in-C static HTML string rather than a dynamic tree constructed via `libbud`. If the shell layout in `nd_ui.c` changes, this fallback string must be manually maintained to match.

### B. NeverDark Micro-Kernel Modularity
- **BCP Decoupling:** Core `bcp.h` is now strictly generic. Game-specific concepts (equipment slots, durability, item wearing) are fully contained within `nd-equip`.
- **UI Slot Registry:** By defining UI slots and action priorities, modules contribute buttons and modals cleanly. In `nd_ui.c`, weak function symbols (`nd_attr_ui_register`, `nd_equip_ui_register`) ensure that if `ux/nd_equip_ui.c` is omitted from the build, zero equipment code is linked, and no equipment UI appears.

### C. Pluggable Viewport Architecture & WebGL
- `nd_viewport_driver_t` provides an extensible abstraction:
  - TTY driver: mounts `<div id="term" data-bud-terminal="1">` (axil-tty).
  - Lamina 2D Canvas driver: mounts `<canvas id="viewport" data-bud-canvas="1">` (liblamina).
  - WebGL driver: mounts `<canvas id="viewport-gl" data-bud-webgl="1">` with initialization and render hooks.
- Action dock automatically injects a viewport cycle button when more than one driver is registered.
- *Capability Status:* `include/ttypt/webgl.h` provides complete C-side WebGL 1/2 function declarations. `htdocs/webgl-bridge.js` provides generic browser bindings conforming to `scripts/check-no-site-specific-js.sh`.

### D. Safe Capability Orchestration in `bud-client.js`
- By pre-compiling WASM modules and inspecting `WebAssembly.Module.imports()`, `bud-client.js` synthesizes `imports.bud` when requested while providing `imports.env` populated with all bridge and capability functions.
- Prevents runtime instantiation errors across mixed-generation modules (`gig_detail.wasm`, `site_chrome.wasm`, `nd.wasm`).

---

## 3. Verification & Quality Gate Results

All validation checks and automated test suites were executed with zero failures:
1. `scripts/check-module-boundaries.sh`: PASS (0 prohibited cross-module includes, no forbidden `var/` literals).
2. `scripts/check-ux-purity.sh`: PASS (0 XY hooks in UX, 0 unexpected preprocessor conditionals).
3. `scripts/check-no-site-specific-js.sh`: PASS (0 site-specific terms in `htdocs/*.js`).
4. `scripts/check-wasm-imports.sh`: PASS (no native symbols in `htdocs/*.wasm`).
5. `scripts/check-no-tree-rpath.sh` & `check-no-impl-guards.sh`: PASS.
6. `tests/unit/run-nd-bcp.sh`: PASS (6/6 tests passing, including dynamic channel dispatch).
7. `mods/nd/test.sh`: PASS (unit tests and artifact presence verified).
8. `external/axil-nd/test.sh`: PASS (`axil-nd ok`, all MUCK lifecycle, terminal, and HTTP tests passed).
9. `make standalone-unit-tests`: PASS (all 23 contract, auth, formatting, and parser test suites passing).

---

## 4. Residual Risks, Technical Debt & Recommendations

1. **Asset Management for `webgl-bridge.js`:**
   Because `.gitignore` ignores `/htdocs/*` by default (except for specific core files), `htdocs/webgl-bridge.js` is currently untracked in git. It should be mirrored into an external component repo (e.g. `external/liblamina/htdocs/` or `external/axil-nd/htdocs/`) and copied during `make assets-sync`, matching `lamina-bridge.js` and `axil-tty-bridge.js`.
2. **Dynamic Standalone SSR Serializer:**
   The `nd_fallback_ssr_html[]` in `external/axil-nd/src/http.c` functions well as a fallback for standalone daemons, but is technically a hardcoded HTML string. For long-term maintainability, standalone axil-nd should compile against a lightweight dynamic serializer to eliminate hardcoded HTML strings entirely.
3. **Dynamic Unload/Reload of UI Extensions:**
   The UI slot registry currently supports registration during startup (`nd_ui_init_defaults()`). If NeverDark's dynamic in-game module unloading (`unloadmod`) is extended to WASM clients at runtime, an unregister API (`nd_ui_unregister_extension`) should be added to tear down buttons and modals when a module is unloaded.
