# Overview — what this project is and how it fits together

Read this first. It is the 5-minute orientation; every claim here is expanded
in a `docs/` file linked below. The golden rule for every other doc:
**the SSR markup + the hyle query API are the only cross-framework interface.**

## The site

A pure-C music site for poems, songs, gigs, and grps. Server-rendered
HTML (SSR-first) with progressive, optional WASM enhancement in the browser.
**No Rust, Dioxus, or Deno in the request path** — the only WASM is C compiled
to wasm32-wasi for browser-side polish.

## The stack (libraries, not a framework)

| Piece | What it does |
|-------|--------------|
| `external/axil` | HTTP, sessions, auth, uploads, chroot |
| `external/libxylem` | XY cross-.so dispatch (`dlopen` before chroot) |
| `external/libcorm` | opaque data store (never free values) |
| `external/liblemma` | tokenization / search fold (accent-sensitive) |
| `external/libhyle` | data/query kernel: canonical schema (`hyle_schema_desc_t`), query, filtering, FTS, generic registry/ordered/derive machinery, FFI row API, `hyle_qs_param` |
| `external/libhyle-source` | standalone persistence engine: dataset metadata, DSV, JSON state overlays, pluggable drivers (`hyle_source_store_ops_t`), plus the picker DTOs (`hyle-source/picker.h`) |
| `external/libhyle-bud` | the bud component binding — the ONLY code that may depend on bud; bridges Hyle schemas to Bud UI components (`index`/`gig`/`grp` and the `/pick/:id/options` fragment route hosted by `mods/source`) |
| `external/axil-hyle` | framework-neutral Axil↔Hyle REST connector — JSON CRUD (`/api/dataset/...`) + partition routes; no bud symbols |
| `external/libbud` | pure C DOM scaffold (SSR), 5-field UI binder (`bud_field_desc_t`), and pure VDOM diff engine (`bud_vdom_diff`) |
| `external/libstele` | universal single-instantiation WASM reactor substrate, UI slots (`STL_SLOT_PAGE`, `STL_SLOT_CHROME`), module lifecycle (`stl_module_ops_t`), and Web HAL host imports (`<ttypt/stele_wasm.h>`) |
| `external/libchaff` | continuous multi-scale 1/f procedural noise engine with GLSL parity and toroidal periodic domain wrapping |
| `external/libareole` | multi-scale spatial hierarchy engine: 16-level LOD quadtree, analytic unprojection, 6-plane frustum culling, and priority refinement |
| `external/liblamina` | 2D software tile blitter and 2.5D/3D WebGL continuous heightfield & Beer-Lambert water optics bridge |
| `external/libpitch` | pure-C musical chord parser and key transposer |
| `external/libjoint` / `libislet` / `libsepal` | temporal interval-tree (Time), Morton spatial index (Space), and 2-stage ANN vector store (Meaning) axes for `libcorm` |
| `external/libhyle-react` | React/Next.js client bindings for Hyle schema and Bud state reconciliation |
| `external/axil-tty` | WebSocket-to-PTY terminal streaming bridge |
| `external/axil-nd` | modular NeverDark MUCK engine running in-process behind Axil |
| `external/axil-auren` | local LLM inference, embedding, and classification engine |
| `external/axil-resume-search` | PDF resume ingestion, vector embedding, and job candidate matching service |
| `mods/*` | site modules — thin composition layers wiring the libraries together (`core`, `common`, `i18n`, `source`, `index`, `auth`, `mpfd`, `redir`, `site_chrome`, `song`, `poem`, `gig`, `grp`, `software`, `nd`, `auren`, `chat`) |

## Framework-pair model (the governing design rule)

Each component framework comes in a **pair**: one SSR implementation and its
own client runtime (bud↔bud-WASM, React↔React-hydration). The two sides of a
pair agree on the **SSR contract** (plain HTML + `data-*` hooks). hyle stays
framework-neutral and carries metadata (including UI hints) **opaquely**;
the component layer interprets it. See `docs/ARCHITECTURE.md`.

## Request path (what happens when a URL loads)

1. `axil` on :8080 receives the request; all handlers are C in `mods/*/*.so`.
2. Modules were dlopen'd at boot (before the chroot) in the order enforced by
   `mods/core/core.c` `xy_install`.
3. A handler collects data via hyle `source_query`, renders HTML with bud
   (or calls a shared C renderer), and responds through the common page
   helpers (`site_ui_respond_page`, …).
4. If the page opts in (`data-modules="list"` etc.), the browser fetches
   `/{module}.wasm`, hydrates via the Stele/Bud bridge, and enhances. No-JS pages
   work identically minus the polish.

## The unbreakable rules

- **No-JS must always work.** Client enhancement is optional and additive.
- **hyle stays neutral.** `external/libhyle` has 0 DOM symbols; `external/libbud` has 0 database/storage symbols. Only `external/libhyle-bud` and `mods/common` may bridge the two.
- **Pure isomorphic WASM boundary.** Interactive frontend UX modules (`mods/*/ux/*.c`) compile to native `.so` for SSR and WASM for the browser, interfacing via `<ttypt/stele_wasm.h>`. Core libraries (`libbud`, `libpitch`, etc.) include only `<ttypt/iso.h>` for pure `ISO_EXPORT` / `ISO_IMPORT` macros without UI substrate dependencies.
- **All row writes go through hyle `put`/`del`** (`source_update_item` /
  `source_delete_item`) — writing corms directly freezes the FTS index. Storage drivers (`store_fs`, `store_mem`) handle file and field persistence automatically.
- **Search is accent-sensitive by design** (`pão` ≠ `pao`); no iconv
  TRANSLIT in the search fold. TRANSLIT survives only in `axil_slugify`.
- **SSR markup is the contract.** `data-bud-*`/patch ops are bud-stack-internal
  and additive; a React consumer implements the same contract independently.

## Repo layout

```
AGENTS.md          doc index — route from here (read this first)
docs/              architectural specs & manuals (OVERVIEW, GOALS, ARCHITECTURE,
                   DESIGN, BUILD, TESTING, CONVENTIONS, STYLING, FILTERS,
                   SCHEMA, PICKERS, WEB_HAL_SPECIFICATION, 25D, RECUR_NOISE,
                   WATER_REQUIREMENTS, SHORE, SEMANTIC_SEARCH, LOCAL_LLM, …)
external/          axil, libxylem, libcorm, libjoint, libislet, liblemma,
                   libsepal, libhyle, libhyle-source, libhyle-bud, libbud,
                   libstele, libchaff, libareole, liblamina, libpitch, axil-nd
mods/              core, common, i18n, source, index, auth, mpfd, redir,
                   site_chrome, song, poem, gig, grp, software, nd, auren, chat
htdocs/            static web root: styles.css, hyle.css, *.wasm, stele-client.js
var/<source>       data directories (server chroots at the repo root)
tests/             unit (per-module test.sh), pages, e2e (Deno+Playwright)
debug/             captured build/runtime/test logs (gitignored)
```

## Bootstrap

```bash
make
make watch          # auto-rebuild + restart on :8080
```

Then open `http://localhost:8080`. The server runs inside a chroot rooted at
the repo root (prereqs in `docs/BUILD.md`).

## Where to go next

| You want to… | Read |
|--------------|------|
| Understand what the architecture wants to be | `docs/GOALS.md` |
| Understand the architecture / load order / XY contract | `docs/ARCHITECTURE.md` |
| Understand the design philosophy before designing anything | `docs/DESIGN.md` |
| Build, rebuild, or hit a build gotcha | `docs/BUILD.md` |
| Run or write tests | `docs/TESTING.md` |
| Write idiomatic C / handlers / follow conventions | `docs/CONVENTIONS.md` |
| Change styling or CSS assets | `docs/STYLING.md` |
| Understand the Web HAL C/WASM systems architecture | `docs/WEB_HAL_SPECIFICATION.md` |
| Follow the Web HAL implementation roadmap | `docs/WEB_HAL_IMPLEMENTATION_PLAN.md` |
| Understand unified client WASM kernel & colocated adapters | `docs/MONOREPO_UNIFIED_WASM_KERNEL.md` / `docs/NEXT_GEN_WASM_ARCHITECTURAL_PLAN.md` / `docs/COLOCATED_NODE_WASM_ADAPTERS.md` |
| Touch filtering / multi-ref semantics | `docs/FILTERS.md` |
| Touch schema strings / UI hints | `docs/SCHEMA.md` |
| Touch omni-dropdown pickers | `docs/PICKERS.md` |
| Build custom semantic & hybrid search (`axil-auren`, `libsepal`, `liblemma`) | `docs/SEMANTIC_SEARCH.md` |
| Configure local LLM inference & multi-axis memory fusion | `docs/LOCAL_LLM.md` / `docs/MULTI_AXIS_MEMORY_FUSION.md` |
| Understand NeverDark 2.5D Seven terrain, noise & LOD hierarchy | `docs/25D.md` / `docs/TERRAIN_ROADMAP.md` / `docs/RECUR_NOISE.md` / `docs/LOD_ELEVATION_RECURSIVE_NOISE_ANALYSIS.md` / `docs/ANALYSIS_TERRAIN_CHUNKS_LOD_STRATOSPHERE.md` / `docs/MULTI_SCALE_NOISE_AND_GPU_ARCHITECTURE.md` |
| Understand physical water optics, shorelines & billboard submergence | `docs/WATER_REQUIREMENTS.md` / `docs/SHORE.md` / `docs/GUIDE_BILLBOARD_SHALLOW_WATER_OPTICS.md` / `docs/handoff-water-realism.md` |
| Understand NeverDark viewports, collision, persistence, BCP & deployment | `docs/ND-VIEWPORTS.md` / `docs/NEXT_GEN_ND_ARCHITECTURE.md` / `docs/NEVERDARK_FULL_UNIVERSE_DEPLOYMENT.md` / `docs/SEVEN_WAYS_COLLISION_ANALYSIS.md` / `docs/AOT_GAME_CONTENTS_ANALYSIS.md` / `docs/ND-PERSISTENCE-LIFECYCLE.md` / `docs/AXIL_ND_MCP_BCP_IMPROVEMENT_SPEC.md` |
| Work with sibling repos, CI/NPM distribution, extensions & git recovery | `docs/SIBLING_REPOS.md` / `docs/CI_NPM_DISTRIBUTION.md` / `docs/RELEASE_EXECUTION_STRATEGY.md` / `docs/EXTENSIONS.md` / `docs/ADVANCED_GIT_RECOVERY.md` |
