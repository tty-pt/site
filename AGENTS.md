# AGENTS.md — documentation index

Pure-C music site (poem, song, gig, grp) on the axil HTTP library, the
libxylem XY module system, the hyle data engine, and the bud HTML builder. SSR-first with
progressive WASM enhancement; no Rust/Dioxus/Deno in the request path.

**Read the relevant doc below before touching code.** Start with
`docs/OVERVIEW.md` — read it first, always.

```bash
make
./start.sh          # singular turnkey runner: auto-seeds universe.db, sets paths, runs on :8080
make watch          # auto-rebuild + restart on :8080
```

## Guidelines — read this, 2 minutes

1. **UX is pure & isomorphic.** `mods/*/ux` compiles twice: native `.so`
   for SSR and `htdocs/*.wasm` for the browser. One `bud_app_render(state)`
   + `bud-state` JSON + `#bud-root` / `#chrome-root` wrapper. Allowed:
   `bud.h` / `bud_jsx.h` / `bud_app.h` + `hyle-bud/hyle-bud.h` + `<ttypt/stele_wasm.h>` + pure C.
   The isomorphic WASM boundary is governed by `<ttypt/stele_wasm.h>` (which transitively
   includes `<ttypt/iso.h>` for `ISO_EXPORT` / `ISO_IMPORT`). Interactive UX modules register
   via `STL_WASM_MODULE(...)` and invalidate slots via `stl_slot_invalidate()`. Standalone libraries
   (`libbud`, `libpitch`) include only `<ttypt/iso.h>` with zero UI substrate dependencies.
   Forbidden in UX: `XY_`/`xy_`/`corm_`/`source_`/`axil_`/`lemma_`/
   `hyle_source_`/`var/` literals. Sanctioned `#include "*.c"` only
   `site_ui.c|list.c` (`scripts/check-module-boundaries.sh`).
   Check `grep -E 'corm_|source_|axil_|hyle_source|XY_' mods/*/ux/*.c` must
   be 0 and `sh scripts/check-wasm-imports.sh` must pass.
   Note: `mods/site_chrome/ux` is WASM-only (`WASM_ONLY=1`) — not a native module.

2. **UX avoids compile-time branching.** No `#if`/`#ifdef` for nodes.
   Branch on runtime `state`. Allowed only: `#ifndef *_C` guards,
   `__attribute__((import_module("env")…))` host imports,
   `site_ui.c:#ifndef __wasm__` aggregator,
   `site_page.c:#if __has_include` fallback (id-drift prevention).

3. **XY is the only cross-.so boundary.** Unconditional `XY_DECL` in caller header,
   `XY_IMPL` in owner, no X_IMPL guards — two-header rule (`M-types.h` for shared types,
   `M.h` for caller `XY_DECL`s; implementer includes `M-types.h` and never `M.h`),
   `static` by default, never `extern` or cross-module `#include "*.c"`.
   Declare deps via `xy_load()` in `xy_install()` — modules are reusable;
   maximally independent = explicit DAG, not zero edges
   (`poem→index`, `song→index+mpfd`, `grp→index+mpfd+song`,
   `gig→index+mpfd+song+source+grp`; `core` loads only `common+source`).
   See `ARCHITECTURE §5`, `CONVENTIONS`.

4. **hyle stays neutral; SSR is the contract.** `external/libhyle` owns
   canonical data schemas (`hyle_schema_desc_t`) without any DOM concepts;
   `external/libbud` is a pure 5-field UI binder (`bud_field_desc_t`) and VDOM
   diff engine (`bud_vdom_diff`) without any database, storage, or runtime reactor
   concepts. `external/libhyle-source` owns
   persistence and storage drivers (`hyle_source_store_ops_t`). `libhyle-bud`
   is the ONLY bud-dependent bridge and **is** used in UX for filters/tables
   (`index`/`gig`/`grp`). SSR emits plain HTML + `data-*` hooks;
   `data-bud-*`/patch ops are additive.

5. **Data invariants.** All row writes via `source_update_item` /
   `source_delete_item` → `hyle put/del` (FTS), never direct `var/`;
   search accent-sensitive `pão≠pao` (no TRANSLIT, only `axil_slugify`);
   No-JS must always work.

6. **No Site-Specific JavaScript.** JavaScript (`htdocs/*.js`) must remain
   strictly generic library infrastructure (`hyle` slot transport, `bud` hydration).
   Zero domain-specific identifiers, URLs, module names (`song`, `gig`, `poem`, `grp`),
   or custom client logic in JS. All rich client behaviors belong in isomorphic
   WASM (`mods/*/ux/*.c`). Enforced via `scripts/check-no-site-specific-js.sh`.

7. **Autonomous Quest Management.** All work MUST use the Quest Journal workflow.
   - The assistant automatically creates and maintains active quests on disk (`.pi/quest/future/<qid>.md`) from user requests (or when activated via `/quest on`, `"quest: Go for it."`, or `/quest new` after conversational planning with Argon).
   - The quest file is your single source of truth for goals, status, and decisions. Update it proactively as you make progress and before context is compacted.
   - Sub-quests are created automatically via `quest_subquest` when tangent remarks or follow-ups arise.
   - Completed work is finalized via `quest_archive` (or `/quest finish`), integrating the scratch worktree into `main` (or retaining `.pi/scratch/slot-N` via `keep-slot` / `squash-keep-slot` for manual testing) and cleaning up `.pi/quest/future/<qid>.md` directly without `.zip` files.
   - Never use ad-hoc scratchpads, `.todo` files, or try to keep the entire plan in your head.
   - **Unified Bundle Packaging**:
     ```text
     After testing or making changes:
         npm --prefix .pi/extensions/pi-quest run zip

     Send pi-quest-bundle.zip containing current code and latest run diagnostics.
     ```

8. **Targeted Verification Hierarchy & Quality Gates.**
   - **Scope-Calibrated Actions First**: When code changes, perform ONLY the targeted rebuild and test actions corresponding to the modified files. Never run `make test` for single-module, UI, or client asset fixes.
   - **Tiered Verification Hierarchy**:
     - **Tier 0 (Targeted Module / UX / Asset)**:
       - *Server-Side C Module* (`mods/<M>/*.c`, `include/*.h`): `make -C mods/<M>` (or `make mods`), then `make test-mod MOD=<M>` (or `(cd mods/<M> && ./test.sh)`).
       - *Client-Side Isomorphic UX* (`mods/<M>/ux/*.c`): dual-compile with `make -C mods/<M>` (SSR) and `make site-wasm` (browser WASM), then verify with `sh scripts/check-module-boundaries.sh` and `sh scripts/check-wasm-imports.sh`.
       - *Client Scripts / WebGL Bridge* (`htdocs/*.js`, `external/liblamina/`): sync to `htdocs/`, then `sh scripts/check-no-site-specific-js.sh` and targeted unit check (e.g. `sh tests/unit/run-test-lamina-bridge.sh`).
       - *Core C Libraries* (`external/lib<L>/`): `make <L>-lib`, then `tests/unit/run-<L>-*.sh` or `make standalone-unit-tests`.
       - *Harness / Tooling* (`.pi/extensions/pi-quest/`): `deno test --allow-all tests/` and `deno run --allow-all scripts/check-complexity.ts`.
     - **Tier 1 (Fast Repo Suite)**: `make test-fast` (runs boundary check, C unit tests, and standalone unit tests in < 3 seconds without spawning background daemons or Deno E2E).
     - **Tier 2 (Integration Suite)**: `make test-integration` (runs pages tests, server daemon tests, and WASM package suites).
     - **Tier 3 (Monolithic Suite)**: `make test` (RESERVED ONLY for cross-cutting core architectural changes affecting the HTTP server daemon, TCP socket pipeline, or primary data engine storage).
   - **Final Quality Gates**: Target builds complete with zero compiler errors, code contains zero debug artifacts (no temp logs or leftover debug code), and the relevant targeted test commands pass cleanly with exit code 0.

9. **Abstraction-First Test-Driven Development & Integration Sequencing.**
   Always implement with tests for each individual abstraction (e.g. `libislet`, `libareole`). Test each abstraction thoroughly with standalone unit suites as its part is completed, then code the integration across consuming modules (e.g. `Seven`, `Lamina`), then test the full end-to-end integration. Never integrate unverified abstractions or attempt monolithic cross-module wiring before unit verification.

## Sibling Repositories & External Services

Operational guides, build steps, and test instructions for sibling ports and auxiliary services (`axil-nd`, `axil-auren`, and `axil-resume-search`) are maintained in `docs/SIBLING_REPOS.md`.

## Topic index

| Topic | Read |
|-------|------|
| Sibling repositories (`axil-nd`, `axil-auren`, `axil-resume-search`) | `docs/SIBLING_REPOS.md` |
| 5-min orientation: stack, framework-pair model, request path, unbreakable rules | `docs/OVERVIEW.md` |
| Local LLM inference, session continuity & Pi agent integration | `docs/LOCAL_LLM.md` |
| Invariants & checklists | `docs/GOALS.md` |
| Encapsulation & abstractions — read before adding a feature | `docs/DESIGN.md` |
| Module graph, load order, XY contract, data invariants | `docs/ARCHITECTURE.md` |
| C style, handlers, form parsing, XY, hyle-bud, preprocessor | `docs/CONVENTIONS.md` |
| Build, WASM rebuild, stale headers, chroot | `docs/BUILD.md` |
| Tests & e2e prereqs | `docs/TESTING.md` |
| Styling & CSS cache bust | `docs/STYLING.md` |
| Filters & schema hints | `docs/FILTERS.md` / `docs/SCHEMA.md` |
| Pickers & Omni-Dropdowns | `docs/PICKERS.md` |
| Extension guide & custom Pi workflows | `docs/EXTENSIONS.md` |
| Advanced Git Recovery | `docs/ADVANCED_GIT_RECOVERY.md` |
| Web HAL specification (C systems web runtime) | `docs/WEB_HAL_SPECIFICATION.md` |
| Web HAL implementation plan (rollout roadmap) | `docs/WEB_HAL_IMPLEMENTATION_PLAN.md` |
| Unified client WASM kernel architecture | `docs/MONOREPO_UNIFIED_WASM_KERNEL.md` |
| Next-gen WASM architectural plan (`libcorm`/`libxylem` in WASM) | `docs/NEXT_GEN_WASM_ARCHITECTURAL_PLAN.md` |
| Colocated Node.js N-API & WebAssembly adapters | `docs/COLOCATED_NODE_WASM_ADAPTERS.md` |
| CI automation & universal C distribution via NPM | `docs/CI_NPM_DISTRIBUTION.md` |
| Ecosystem release execution strategy | `docs/RELEASE_EXECUTION_STRATEGY.md` |
| Custom semantic & hybrid search (`axil-auren`, `libsepal`, `liblemma`) | `docs/SEMANTIC_SEARCH.md` |
| Multi-axis memory fusion (`axil-auren`, `pi-mm`, `libcorm` axes) | `docs/MULTI_AXIS_MEMORY_FUSION.md` |
| Realistic pure-C water for the NeverDark Seven stage (handoff) | `docs/handoff-water-realism.md` |
| Shallow water optical extinction calibration for billboards | `docs/GUIDE_BILLBOARD_SHALLOW_WATER_OPTICS.md` |
| NeverDark 2.5D Seven continuous 3D terrain renderer specification | `docs/25D.md` |
| NeverDark procedural 3D terrain & cavern architecture roadmap | `docs/TERRAIN_ROADMAP.md` |
| Recursive noise algorithm & `libchaff` mathematical foundation | `docs/RECUR_NOISE.md` |
| LOD elevation growth & recursive noise blending analysis | `docs/LOD_ELEVATION_RECURSIVE_NOISE_ANALYSIS.md` |
| Client-side chunk merging, LOD smoothing & stratosphere camera | `docs/ANALYSIS_TERRAIN_CHUNKS_LOD_STRATOSPHERE.md` |
| Physical water optics, volumetric absorption & rendering specification | `docs/WATER_REQUIREMENTS.md` |
| Seamless shoreline & continuous bathymetry ("chunks can't disagree") | `docs/SHORE.md` |
| Seven-Ways collision map analysis & 2.5D obstacle modeling | `docs/SEVEN_WAYS_COLLISION_ANALYSIS.md` |
| Ahead-of-Time (AOT) vs. on-room-entry game content architecture | `docs/AOT_GAME_CONTENTS_ANALYSIS.md` |
| NeverDark entity persistence lifecycle & Corm DB consistency | `docs/ND-PERSISTENCE-LIFECYCLE.md` |
| `axil-nd` MCP/BCP protocol architecture & modernization | `docs/AXIL_ND_MCP_BCP_IMPROVEMENT_SPEC.md` |
| Multi-scale noise, client GPU acceleration & stratosphere architecture | `docs/MULTI_SCALE_NOISE_AND_GPU_ARCHITECTURE.md` |
| Next-generation NeverDark architecture (single-socket WS, BCP, hydration) | `docs/NEXT_GEN_ND_ARCHITECTURE.md` |
| NeverDark full universe deployment, seeder & E2E harness | `docs/NEVERDARK_FULL_UNIVERSE_DEPLOYMENT.md` |
| NeverDark pluggable viewports (ANSI TTY, 2D Lamina, 3D WebGL) | `docs/ND-VIEWPORTS.md` |
| Hyle ecosystem & cross-framework convergence (React/Next.js) | `ROADMAP.md` |
| Axil PDF resume ingestion & job search service (`axil-resume-search`) | `external/axil-resume-search/README.md` |
