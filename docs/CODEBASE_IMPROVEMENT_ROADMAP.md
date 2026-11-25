# Codebase Improvement Roadmap

Phased, dependency-ordered plan to make the codebase leaner, faster to build and run, and easier to understand — while preserving all SSR/No-JS contracts, XY module boundaries, and existing userspace/test invariants.

---

## Overview

| Phase | Theme | Effort | Risk | Status | Headline Impact |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **P0** | Asset Sync & Single Source of Truth | Small (1–2h) | Low | **Done** | Fixes live `bud-cap.js` drift; makes `htdocs/` strictly derived |
| **P1** | WASM Size, Incremental Caching & Unified Bundle Cutover | Small–Med (½d) | Low–Med | **Done** | 4–7× smaller `.wasm` payloads; 0.77s incremental `nd.wasm`; single cached `/site.wasm` on site pages |
| **P2** | `libbud` Bump Arena (`bud_arena`) | Medium (1d) | Medium | **Done** | 100% fewer per-frame heap allocs (474,000 → 0 over 1,000 renders), 2.7–4.7× faster tree build, zero ASan/UBSan findings |
| **P3** | Build & Unit-Test Deduplication | Small–Med (½d) | Low | **Done** | Shrinks `Makefile` (-142L) & 18 unit scripts (-512L raw / -271L net); 7.1× faster warm ND UX tests; 31/31 `standalone-unit-tests` green |
| **P4** | Seven Engine Extraction & Shader Deduplication | Large (2–3d) | Medium | **Done** | Cuts ~2,000L duplicate GLSL, splits 1,756L `nd_seven_render` → 68L, shrinks `mods/nd/ux/` 31K → 5K LOC |
| **P5** | Repo & Submodule Hygiene (Phase 5-lite) | Small (1–2h) | Low | **Done** | Tiered `worktree.sh` (skips 5.4 GB optional siblings by default), retires `pi-quest`, cleans root (-3.9 MB PNGs, -8 stubs, +3 specs in `docs/`), fixes `libjarvis.so` tree RPATH (`make test-fast` green) |

---

## Phase 0 — Asset Sync & Single Source of Truth

**Problem:** `Makefile` `stage-assets` used `cp -u -R "$$d/htdocs/." htdocs/` with a prerequisite-less `htdocs/.assets-staged` stamp, which silently hid edits made directly in `htdocs/` (specifically `htdocs/bud-cap.js` diverged from `external/libbud/htdocs/bud-cap.js` with `wheel` + `button,buttons` changes).

**Deliverables:**
1. Upstream the `htdocs/bud-cap.js` diff into `external/libbud/htdocs/bud-cap.js` and `external/libbud/npm/dist/bud-cap.js`.
2. Update `stage-assets` in `Makefile` to track `ASSET_SOURCES := $(wildcard external/*/htdocs/* mods/*/htdocs/*)` and copy unconditionally (`cp -f -R`) so `external/*/htdocs/` and `mods/*/htdocs/` are always authoritative over `htdocs/`.
3. Add `scripts/check-asset-sync.sh` and wire it into `boundary-check` (`make test-fast`) so any drift fails fast.

**Exit Criteria (Verified):**
- `sh scripts/check-asset-sync.sh` exits `0` (0 drifted files across `external/*/htdocs` and `mods/*/htdocs`).
- `sh scripts/check-no-site-specific-js.sh`, `make -C external/libbud test`, and `npm --prefix external/libbud/npm test` (21/21) exit `0`.

---

## Phase 1 — WASM Size, Incremental Caching & Unified Bundle Cutover

**Problem:**
- Default `PROFILE=dev` in `build.mk` compiled WASM with `-g -O0`: `htdocs/nd.wasm` was **4.2 MB** (1.66 MB DWARF), and the 6 small site modules totaled **3.25 MB**.
- `build.mk` recompiled all 10 `WASM_COMMON_SRC` files + every module TU in one `clang` invocation on every edit.
- `Makefile` already built a unified `htdocs/site.wasm`, but `site_page.c` never emitted `data-wasm-bundle="/site.wasm"`, `auren` was missing from `site_modules.c`, `site.wasm` leaked single-module `wasm_init`/`wasm_action` exports and dead `xylem` imports, and `stele-client.js` tried `/${moduleName}.wasm` before `data-wasm-bundle`.

**Deliverables:**
1. **Default WASM flags (`build.mk`)**: Switch default WASM builds to `-Os -DNDEBUG` + `-Wl,--strip-debug` (and `wasm-strip` when present). Keep full DWARF `-g -O0` under `PROFILE=debug` and `make wasm-debug`. Probe `clang-18` before including `portable.mk` so standalone `make -C mods/<M> wasm` works reliably.
2. **Per-TU `.wasm.o` caching (`build.mk`)**: Compile `WASM_COMMON_SRC` and each module TU to cached `.wasm.o` files (keyed by compiler flag hash, with `.d` dependency tracking and atomic rename for `-j` safety) before linking `$(WASM_PATH)/%.wasm`.
3. **Unified `/site.wasm` cutover**:
   - Build `htdocs/site.wasm` with `-Os -DNDEBUG -DQSYS_UNIFIED_SITE -Wl,--strip-debug` (+ `wasm-strip`), removing unused `xylem.o`.
   - Add `auren` (`auren_fe.c` + `auren_ui.cx.c`) to `SITE_WASM_OBJS` and `mods/common/ux/site_modules.c`.
   - Guard single-module `wasm_init` / `wasm_init_bin` / `wasm_action` / `wasm_fetch_callback` exports with `#ifndef QSYS_UNIFIED_SITE` in `mods/common/site_wasm_app.h` and `mods/site_chrome/ux/chrome.c`; track per-module mounted slot and `s_active_slot` in `site_wasm_app.{h,c}` so multi-slot dispatch and `bud_app_set_state()` target the active slot cleanly.
   - Emit `data-wasm-bundle="/site.wasm"` on `<body>` in `mods/common/ux/site_page.c`.
   - Update `external/libstele/htdocs/stele-client.js` (and `external/libstele/npm/dist/client.js`) so `mountSlot()` (both main-thread and Web Worker modes) prefers `data-wasm-bundle` first, reuses one instantiated bundle across slots via `stl_kernel_mount` (falling back to `/${moduleName}.wasm` when a module like `nd` is absent), and routes actions/updates/events through `stl_kernel_*`.
   - Keep per-module `WASM_TARGETS` intact (now `-Os` + stripped + fast-cached) so zero existing tests or standalone workflows break.

**Exit Criteria (Verified):**
- Payload sizes: `htdocs/nd.wasm` **4.2 MB → 1.2 MB** (-71%); `htdocs/site.wasm` **354 KB → 166 KB** (-53%, now including `auren`, 9 clean imports, zero single-module leaks); `site_chrome.wasm` **449 KB → 61 KB** (-86%); `song.wasm` **569 KB → 85 KB** (-85%); `gig.wasm` **652 KB → 96 KB** (-85%); `list.wasm` **680 KB → 116 KB** (-83%); `chat.wasm` **458 KB → 64 KB** (-86%); `auren.wasm` **442 KB → 60 KB** (-86%).
- Incremental `nd.wasm` rebuild after 1-TU touch (`touch mods/nd/ux/nd_hud_ui.cx && make -C mods/nd wasm`): **0.77s**.
- `node --test tests/unit/test_site_wasm_kernel.mjs tests/unit/test_wasm_event_bus.mjs tests/unit/test_wasm_optimistic.mjs tests/unit/test-stele-client-spa.mjs` (10/10), `sh scripts/check-wasm-imports.sh`, `sh external/mk/scripts/wasm-audit.sh htdocs/*.wasm`, and `sh external/mk/scripts/wasm-audit.sh --check-substrate` exit `0`.

---

## Phase 2 — `libbud` Per-Render Bump Arena (`bud_arena`)

**Problem:** `external/libbud/src/libbud.c` performs ~10 `calloc`/`strdup`/`free` calls per DOM element, causing heap churn in SSR and fragmentation in WASM linear memory on every VDOM reconcile frame.

**Deliverables:**
1. Defined chunked slab `bud_arena` (`8 KB` initial, doubling up to `64 KB`, warm-slab retention up to `256 KB` on reset, oversized slabs freed on reset) in `external/libbud/src/bud_internal.h` + `src/libbud.c` with thread-local active-arena scoping (`bud_arena_new`, `bud_arena_push`, `bud_arena_pop`, `bud_arena_current`, `bud_arena_reset`, `bud_arena_free`, `bud_arena_bytes_used`).
2. Packed unified `uint8_t` ownership flags (`*_ARENA`, `*_OWNED`) into `bud_node`, `bud_attr`, and `bud_listener` (zero struct size growth), added zero-copy static-literal APIs (`bud_element_lit`, `bud_text_lit`, `bud_set_attr_lit`, `bud_set_attr_name_lit`, `bud_set_bool_attr_lit`, `bud_set_key_lit`, `bud_bind_ctx_lit`), and updated `cx_transpile.c` to emit `*_lit` calls for compile-time JSX string literals.
3. Wired pooled per-render `bud_arena` into `bud_reconcile.c` (`bud_mount`), `mods/common/site_wasm_app.c` (with re-entrant deferred retirement), `external/libbud/src/bud_wasm_app.c`, and SSR chrome rendering in `mods/common/ux/site_page.c` (also plugging the pre-existing `chrome` VDOM leak).
4. Added standalone unit tests (slab growth, oversized slab release, nested push/pop + `NULL` heap escape, mixed heap+arena trees, mutations, cross-arena `bud_vdom_diff`, `bud_expand_tree`, runtime dispatch + `on_unmount`) and a 1,000-render ~105-node benchmark in `external/libbud/src/bud_test.c`.
5. Synced `external/libbud/npm/include/bud/bud.h`, rebuilt & installed `libbud` (`PREFIX="$HOME/.local"`), rebuilt `npm/dist/bud.wasm`, re-transpiled all 25 `.cx` files, and rebuilt all native `.so` modules, `htdocs/site.wasm`, and `htdocs/nd.wasm` with zero API breaks to `bud_free()`.

**Exit Criteria (Verified):**
- `make -C external/libbud EXTRA_CFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -O1" LDLIBS="-fsanitize=address,undefined" test` exits `0` with 0 ASan/LSan/UBSan findings (`bud_test`, `bud_reconcile_test`, `cx_test`, `cx_lsp_test`).
- 1,000-render benchmark (~105 nodes/frame): **474,000 → 0 heap allocations (100.00% reduction)**; wall time **38.3 → 12.1 µs/frame (3.16× faster)** unoptimized (`-O0`) and **172.1 → 36.5 µs/frame (4.72× faster)** under ASan+UBSan `-O1`.
- `npm --prefix external/libbud/npm test` (21/21), `node --test tests/unit/test_{site_wasm_kernel,wasm_event_bus,wasm_optimistic}.mjs tests/unit/test-stele-client-spa.mjs` (10/10), `make unit-c-tests` (8/8), `make test-mod MOD=song`, `make test-mod MOD=nd`, `run-site-ui-helpers.sh`, `run-bud-form.sh`, `run-externals-abstractions.sh`, `run-test-{seven-stage-html,ssr-hydration,viewport-registry}.sh`, and all asset/JS/boundary/purity/WASM audits exit `0`.

---

## Phase 3 — Build System & Unit-Test Harness Deduplication

**Problem:**
- `Makefile` repeats ~30 nearly identical `*-lib:` targets and 28 copy-pasted `SITE_WASM_OBJS` compile rules.
- `tests/unit/` has 59 scripts (1,375 LOC) duplicating flags, `.cx` transpilation, and the 15-file ND UX source list across 5 separate scripts.

**Deliverables:**
1. Replaced all 21 repetitive `*-lib:` rules (including stranded `xylem-lib` / `jarvis-lib`) in `Makefile` with a `LIB_TARGET` template macro propagating `EXT_MAKE_ARGS`, `PREFIX`, `PROFILE`, and `SANITIZE` uniformly (fixing missing `EXT_MAKE_ARGS` on `areole-lib`, `chaff-lib`, `corolla-lib`), and deduplicated `stage-assets` `@tty-pt/*` symlinks + `npm-test-all`/`standalone-unit-tests`/`test-graphics`/`pages-test` script lists.
2. Collapsed the 28 copy-pasted `SITE_WASM_OBJS` rules in `Makefile` into a declarative `SITE_WASM_SPECS` table + `SITE_WASM_RULE` macro with `-MMD -MP -MT $@ -MF $@.d` automatic header tracking and atomic `.tmp` renames.
3. Added `tests/unit/common.sh` with automatic repo-local `UNIT_EXT_INCLUDES` / `UNIT_EXT_LDFLAGS` / `UNIT_LD_LIBRARY_PATH`, incremental `unit_cx_transpile`, and cached `-MMD -MP` static archives (`libnd_ux_test.a` with `--whole-archive` constructor preservation and `libnd_seven_test.a`). Migrated 18 unit scripts (5 full ND UX, 4 Seven graphics, 9 `-lbud` standalone scripts) and fixed `tests/unit/site_media_test.c` (`site_media.cx.c`).

**Exit Criteria (Verified):**
- `Makefile` shrunk **682 → 540 lines (-142L)** with all 26 `*-lib` targets/aliases preserved; `tests/unit/run-*.sh` shrunk **1,381 → 869 lines (-512L raw, -271L net including `common.sh`)** — **-413 net lines** total.
- `make standalone-unit-tests` exits `0` (**31/31 PASS** in 6.30s with zero custom env overrides); 3 ND UX tests (`seven-stage-html`, `ssr-hydration`, `viewport-registry`) drop from **19.85s → 9.14s cold (2.17×) → 2.80s warm (7.09×)** with verified header invalidation; 4 Seven graphics tests (`scene-sync`, `seven-world`, `map-render`, `seven-render`) all exit `0`.
- All asset, boundary, UX purity, JS, WASM, pkg-config, and `Makefile` `-L` audits exit `0` *(note: `scripts/check-no-tree-rpath.sh` still flags pre-existing external daemon `external/libjarvis/lib/libjarvis.so` outside Phase 3 scope)*.

---

## Phase 4 — Seven 2.5D/3D Engine Extraction & Shader Deduplication ✅ COMPLETED

**Problem:** `mods/nd` is 31,483 LOC (64% of all `mods/`): `nd_seven_gl_shaders.c` (5,720L) duplicates GLSL functions 2–9× into `.rodata`, `nd_seven_render()` spans 1,756 lines in one function, and `nd_seven_atlas.h` (1,235L) / `nd_seven_mesh3d.h` (968L) embed implementations in headers.

**Deliverables (unit-tested at each step):**
1. Split header-embedded data/implementations: `nd_seven_atlas.h` (**1,235 → 61L**) + `nd_seven_atlas.c` (1,186L) and `nd_seven_mesh3d.h` (**968 → 73L**) + `nd_seven_mesh3d.c` (915L); updated `scripts/gen-seven-atlas.py` to emit split `.h`/`.c`.
2. Deduplicated all 18 repeated GLSL macros (`ND_GLSL_*`, `CHAFF_GLSL_*`) and 6 multi-copy GLSL helper blocks (`get_h`, Catmull-Rom, wind, 3D chaff/caustics, water waves, shore swash/wake) in `nd_seven_gl_shaders.c` into single-copy `.rodata` chunks + lazy one-time `nd_glsl_assemble()` behind the existing `nd_seven_gl_{vs,fs}_*()` getters (replacing 25 `extern const char *` globals in `nd_seven_gl_internal.h` with 4 internal getters).
3. Decomposed `nd_seven_render()` (**1,756 → 68L**) into 8 focused `static` pass functions (`seven_emit_near_room_billboards`, `seven_emit_living_meadow`, `seven_emit_forest_floor_logs`, `seven_emit_medium_botanical`, `seven_emit_large_undergrowth`, `seven_emit_geobreak_props`, `seven_emit_particles_and_sort_batch`, `seven_emit_homogeneous_scenery`) sharing a stack-allocated `seven_frame_ctx_t`.
4. Extracted all 23 `nd_seven*` engine files (26,142 LOC) from `mods/nd/ux/` into `mods/nd/seven/` behind a one-way boundary enforced by `scripts/check-module-boundaries.sh`, updated `mods/nd/Makefile` (`ND_SEVEN_PARTS` / `ND_SEVEN_HDRS`, verified portable under both GNU `make` and BSD `bmake`), `tests/unit/common.sh`, standalone test runners, `tests/e2e/water-harness.mjs`, and `scripts/gen-seven-atlas.py`.

**Exit Criteria (Verified):**
- `mods/nd/ux/` dropped from **31,277 → 5,020 authored LOC** (-84.0%); `nd_seven_gl_shaders.c` shrank **5,720 → 5,464L (-256L)** while `htdocs/nd.wasm` shrank **1,176,553 → 1,041,521 bytes (-135,032 bytes / -131.9 KB / -11.5%)** via `.rodata` deduplication; `nd_seven_render()` shrank **1,756 → 68L**.
- Both `make -C mods/nd` and `bmake -C mods/nd` build cleanly (`wasm-audit: PASS`), `make test-mod MOD=nd` exits `0`, `make test-graphics` passes **14/14** (`exit 0`, including `glslangValidator` ES 3.00 compile+link of all 12 programs), and all 7 static audits (`check-module-boundaries.sh`, `check-ux-purity.sh`, `check-wasm-imports.sh`, `check-nd-encapsulation.sh`, `check-nd-no-static-load.sh`, `check-no-site-specific-js.sh`, `check-no-impl-guards.sh`) exit `0`.

---

## Phase 5 — Repo & Submodule Hygiene (Phase 5-lite) ✅ COMPLETED

**Problem:**
- `scripts/worktree.sh` unconditionally linked all 47 submodules into every fresh worktree, including `external/axil-auren` (2.8 GB) and `external/libjarvis` (2.6 GB), and copied 14 MB of deactivated `.pi/extensions/pi-quest/`.
- Repo root carried 8 obsolete redirect stubs, 4 loose scratch/design notes, 3 misplaced active specs, and 3.9 MB of unreferenced binary PNGs (`logos.png`, `stage-seven-screenshot.png`).
- `external/libjarvis/Makefile` recorded a hardcoded source-tree RPATH (`${CURDIR}/../libstele/lib`), causing `scripts/check-no-tree-rpath.sh` and `make test-fast` to fail.

**Deliverables:**
1. **Tiered worktree submodule provisioning (`scripts/worktree.sh`)**: Classified `OPTIONAL_SUBMODULES` (`external/axil-auren`, `external/libjarvis`, `external/libhyle-react`, `external/libmm`, `external/pi-instruct`, ~5.4 GB combined). `worktree.sh init` / `create` now links only the 41 core submodules by default and accepts `--all` to opt into optional sibling repos.
2. **Retired `.pi/extensions/pi-quest`**: Deinitialized and removed the deactivated `pi-quest` submodule from `.gitmodules` and `.pi/extensions/`, cleaned its stale stanza from `.pi/settings.json`, and updated `AGENTS.md`.
3. **Repo root cleanup (zero `docs/` churn)**: Relocated active specs (`OCCLUSION.md`, `OPTIMIZATION.md`, `WORLD_MATH.md`) into `docs/` with updated links in `README.md`, `CHANGELOG.md`, and `AGENTS.md`; archived loose design notes (`GAME_IDEA.md`, `IMPROVE.md`, `PROMPT.md`, `LINKEDIN_POST.txt`) into `docs/archive/` (`docs/archive/README.md` updated); removed 8 obsolete redirect stubs (`25D.md`, `FIX.md`, `NO_WIZ.md`, `RECUR_NOISE.md`, `SAFE_SH.md`, `SHORE.md`, `ST.md`, `TERRAIN_ROADMAP.md`) and 3.9 MB of unreferenced root PNGs (`logos.png`, `stage-seven-screenshot.png`).
4. **Build-tree RPATH fix (`external/libjarvis/Makefile`, `external/libjarvis/tests/Makefile`)**: Removed hardcoded `${CURDIR}/../libstele/lib` RPATHs from `libjarvis.so`, `bin/jarvis`, `bin/jarvisd`, and `mods/*/*.so` (retaining relative `$ORIGIN` / `@loader_path`) and passed `LD_LIBRARY_PATH` to in-tree CLI smoke tests.
5. **Explicitly deferred (no speculative churn)**: Splitting stable 800–1,200L server modules (`gig.c`, `chat.c`, `index.c`, `auth.c`) and reorganizing `docs/` into nested subdirectories were intentionally deferred to avoid breaking cross-doc links and introducing cross-TU churn without an active bug or feature.

**Exit Criteria (Verified):**
- `sh scripts/check-no-tree-rpath.sh` exits `0` (`check-no-tree-rpath: ok`; `libjarvis.so` `RPATH=[$ORIGIN]`).
- `make test-fast` exits `0` (`Fast test suite passed!` across all boundary, C unit, and standalone unit suites).
- Fresh worktree round-trip (`sh scripts/worktree.sh create test-p5-worktree` → verify 41 core linked, 5 optional skipped, `pi-quest` absent, clean `git status`, `make -C mods/poem` builds → `sh scripts/worktree.sh remove`) exits `0` in ~3s.
- Repo root contains only standard project entrypoints (`AGENTS.md`, `CHANGELOG.md`, `LICENSE`, `Makefile`, `README.md`, `RELEASES.md`, `ROADMAP.md`, `build.mk`, `cleanup.sh`, `start.sh`, `test.sh`, configs).
