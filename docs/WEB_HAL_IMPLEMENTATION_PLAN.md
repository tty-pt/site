# Web HAL Implementation Plan: Sequenced Rollout Roadmap

## 1. Roadmap Overview & Timeline

The implementation of the Web Hardware Abstraction Layer (Web HAL) converges NeverDark, the main site, and external projects onto an isomorphic systems architecture across 5 sequenced rollout milestones.

```
+-----------------------------------------------------------------------------+
|                     WEB HAL SEQUENCED ROLLOUT ROADMAP                       |
+-----------------------------------------------------------------------------+
|  Milestone 1: Web HAL Interface & Host Bridge Consolidation                 |
|  - Uniform factory signature across all htdocs/*-bridge.js modules.         |
|  - Zero-copy linear memory views without detached buffer leaks.             |
+-----------------------------------------------------------------------------+
|  Milestone 2: Unified Isomorphic Frame Loop Contract (wasm_tick)            |
|  - Export wasm_tick(float dt, int w, int h) in libbud reactor core.        |
|  - Host requestAnimationFrame driver in htdocs/bud-client.js.               |
+-----------------------------------------------------------------------------+
|  Milestone 3: NeverDark Multi-Modal Viewport Streamlining                   |
|  - Lamina 2D and WebGL 3D viewports render directly during wasm_tick.       |
|  - Bud restricted strictly to HUD text overlays and modal dialogs.          |
|  - Direct binary struct unpacking for streaming BCP packets.                |
+-----------------------------------------------------------------------------+
|  Milestone 4: Site Shell & Feature Module Harmonization                     |
|  - Site shell and feature modules adopt declarative capability loading.     |
|  - Media and audio slots leverage standard host capability bus.             |
+-----------------------------------------------------------------------------+
|  Milestone 5: Standalone SDK Packaging & Quality Assurance                  |
|  - Header consolidation into portable include directories.                  |
|  - Comprehensive E2E verification across all 121 browser test suites.       |
+-----------------------------------------------------------------------------+
```

---

## 2. Milestone Breakdown

### Milestone 1: Web HAL Interface & Host Bridge Consolidation
- **Objective**: Standardize all browser bridge scripts into orthogonal, zero-copy capability providers.
- **Deliverables**:
  - Audit and refactor `htdocs/axil-bridge.js`, `htdocs/lamina-bridge.js`, `htdocs/webgl-bridge.js`, `htdocs/xylem-bridge.js`, and `htdocs/bud-hydrate.js`.
  - Enforce uniform factory signature: `(root, getWasm) => { makeImports(), mount(), destroy() }`.
  - Ensure zero-copy memory access via `instance.exports.memory.buffer` without creating stale typed array views across WebAssembly linear memory growth.

### Milestone 2: Unified Isomorphic Frame Loop Contract
- **Objective**: Establish the 60fps frame loop contract between browser `requestAnimationFrame` and WASM C code.
- **Deliverables**:
  - Export `void wasm_tick(float dt, int width, int height)` in `external/libbud/src/bud_wasm_app.c`.
  - In `htdocs/bud-client.js`, start an animation frame loop when the active reactor exports `wasm_tick`.
  - Integrate delta-time computation and ensure zero heap allocations during the render loop.

### Milestone 3: NeverDark Multi-Modal Viewport Streamlining
- **Objective**: Transition NeverDark viewports to direct systems rendering, unburdening Bud from non-DOM graphics.
- **Deliverables**:
  - Refactor `mods/nd/ux/nd_ui.c` so that active viewports (Lamina 2D, WebGL 3D, ANSI Terminal) update directly inside `wasm_tick`.
  - Restrict Bud's role strictly to HUD text overlays, inventory paperdoll grids, and modal dialogs.
  - Stream binary BCP packets directly into plain C structs (`player_stats_t`, `room_desc_t`) without JSON intermediaries.

### Milestone 4: Site Shell & Feature Module Harmonization
- **Objective**: Align song, gig, and index modules with the Web HAL capability model.
- **Deliverables**:
  - Maintain SSR-first HTML rendering via `site_ui_page_lang` with clean semantic landmarks.
  - Load browser capabilities declaratively via `data-capabilities="bud"` or `data-capabilities="bud axil-ws"`.
  - Verify full progressive enhancement and zero-JS degradation across all pages.

### Milestone 5: Standalone SDK Packaging & Quality Assurance
- **Objective**: Package the architecture for standalone portability and verify full test compliance.
- **Deliverables**:
  - Consolidate reusable headers (`bud.h`, `bud_slot.h`, `bud_state.h`, `xy.h`, `lam.h`, `webgl.h`, `axil-ws.h`).
  - Verify with all repository boundary scripts (`check-module-boundaries.sh`, `check-ux-purity.sh`, `check-no-site-specific-js.sh`, `check-wasm-imports.sh`).
  - Execute full test suite (`make test`) verifying unit tests, integration tests, and all 121 browser E2E tests pass cleanly.

---

## 3. Dependency Graph & Risk Mitigation

```
[Milestone 1: Web HAL Bridges]
       |
       v
[Milestone 2: Frame Loop Contract (wasm_tick)]
       |
       +-------------------------------+
       |                               |
       v                               v
[Milestone 3: NeverDark Viewports]  [Milestone 4: Site Modules]
       |                               |
       +-------------------------------+
       |
       v
[Milestone 5: SDK Packaging & Full E2E Verification]
```

| Risk | Impact | Mitigation |
| :--- | :--- | :--- |
| **WASM Memory Growth** | Detaches cached typed array views in JS host bridges | Re-slice typed arrays from `instance.exports.memory.buffer` on each frame / event dispatch |
| **Frame Loop CPU Spike** | Background tabs consuming unnecessary CPU cycles | Automatically pause `requestAnimationFrame` loop when `document.hidden` is true |
| **Pre-Link Violations** | Server symbols accidentally called in client WASM code | Automatic pre-link source scanner in `build.mk` aborts compilation with line diagnostics |
| **No-JS Regressions** | Form submissions or navigation failing without JavaScript | Semantic HTML forms and CSS peer-selectors remain the bedrock contract |
