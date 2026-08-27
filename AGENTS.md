# AGENTS.md — documentation index

Pure-C music site (poem, song, gig, grp) on the axil HTTP library, the
libxylem XY module system, the hyle data engine, and the bud HTML builder. SSR-first with
progressive WASM enhancement; no Rust/Dioxus/Deno in the request path.

**Read the relevant doc below before touching code.** Start with
`docs/OVERVIEW.md` — read it first, always.

```bash
make
make watch          # auto-rebuild + restart on :8080
```

## Guidelines — read this, 2 minutes

1. **UX is pure & isomorphic.** `mods/*/ux` compiles twice: native `.so`
   for SSR and `htdocs/*.wasm` for the browser. One `bud_app_render(state)`
   + `bud-state` JSON + `#bud-root` / `#chrome-root` wrapper. Allowed:
   `bud.h` / `bud_jsx.h` / `bud_app.h` + `hyle-bud/hyle-bud.h` + pure C.
   Forbidden in UX: `XY_`/`xy_`/`qmap_`/`source_`/`axil_`/`stoma_`/
   `hyle_source_`/`var/` literals. Sanctioned `#include "*.c"` only
   `site_ui.c|list.c|music.c` (`scripts/check-module-boundaries.sh`).
   Check `grep -E 'qmap_|source_|axil_|hyle_source|XY_' mods/*/ux/*.c` must
   be 0 and `sh scripts/check-wasm-imports.sh` must pass.
   Note: `mods/site_chrome/ux` is WASM-only (`WASM_ONLY=1`) — not a native module.

2. **UX avoids compile-time branching.** No `#if`/`#ifdef` for nodes.
   Branch on runtime `state`. Allowed only: `#ifndef *_C` guards,
   `__attribute__((import_module("env")…))` host imports,
   `site_ui.c:#ifndef __wasm__` aggregator,
   `site_page.c:#if __has_include` fallback (`C-ISOMORPHIC-BUD §3`).

3. **XY is the only cross-.so boundary.** `XY_DECL` in header behind
   `#ifndef MODULE_IMPL`, `XY_IMPL` in owner, constants outside guard,
   `static` by default, never `extern` or cross-module `#include "*.c"`.
   Declare deps via `xy_load()` in `xy_install()` — modules are reusable;
   maximally independent = explicit DAG, not zero edges
   (`poem→index`, `song→index+mpfd`, `grp→index+mpfd+song`,
   `gig→index+mpfd+song+source+grp`; `core` loads only `common+source`).
   See `ARCHITECTURE §5`, `CONVENTIONS`.

4. **hyle stays neutral; SSR is the contract.** `external/hyle` owns
   canonical data schemas (`hyle_schema_desc_t`) without any DOM concepts;
   `external/bud` is a pure 5-field UI binder (`bud_field_desc_t`) without
   any database/storage concepts. `external/hyle/c/libhyle-source` owns
   persistence and storage drivers (`hyle_source_store_ops_t`). `libhyle-bud`
   is the ONLY bud-dependent bridge and **is** used in UX for filters/tables
   (`index`/`gig`/`grp`). SSR emits plain HTML + `data-*` hooks;
   `data-bud-*`/patch ops are additive. See `SSR-CONTRACT`.

5. **Data invariants.** All row writes via `source_update_item` /
   `source_delete_item` → `hyle put/del` (FTS), never direct `var/`;
   search accent-sensitive `pão≠pao` (no TRANSLIT, only `axil_slugify`);
   No-JS must always work (`SSR-CONTRACT`).

6. **No Site-Specific JavaScript.** JavaScript (`htdocs/*.js`) must remain
   strictly generic library infrastructure (`hyle` slot transport, `bud` hydration).
   Zero domain-specific identifiers, URLs, module names (`song`, `gig`, `poem`, `grp`),
   or custom client logic in JS. All rich client behaviors belong in isomorphic
   WASM (`mods/*/ux/*.c`). Enforced via `scripts/check-no-site-specific-js.sh`.

7. **Task Management.** All work MUST use the Task Journal workflow.
   - Use `/task <name>` to start or resume active work (`docs/current/<name>.md`).
   - The task file is your single source of truth for goals, status, and decisions. You must update it as you make progress and before context is compacted.
   - Use `/task-draft <name>` to propose planned work in `docs/future/`.
   - Completed work is archived to `docs/archive/` via `/task-del`.
   - Never use ad-hoc scratchpads, `.todo` files, or try to keep the entire plan in your head.

7. **Test-Driven Development (TDD) & Quality Gates.**
   - **Build & Run First**: Discover how to build (`make`) and run (`make watch`) the project before editing feature code.
   - **Write Tests First**: Develop unit/integration/E2E tests (`make test`) BEFORE writing feature code.
   - **Iterative Loop**: Feature implementation $\rightarrow$ `make` $\rightarrow$ run/verify $\rightarrow$ test.
   - **Final Quality Gates**: Build completes with zero errors, code contains zero debug artifacts (no temp logs or leftover debug code), and the full test suite (`make test`) passes with zero failures.

## Topic index

| Topic | Read |
|-------|------|
| 5-min orientation: stack, framework-pair model, request path, unbreakable rules | `docs/OVERVIEW.md` |
| Invariants & checklists | `docs/GOALS.md` |
| Encapsulation & abstractions — read before adding a feature | `docs/DESIGN.md` |
| Module graph, load order, XY contract, data invariants | `docs/ARCHITECTURE.md` |
| C style, handlers, form parsing, XY, hyle-bud, preprocessor | `docs/CONVENTIONS.md` |
| Build, WASM rebuild, stale headers, chroot | `docs/BUILD.md` |
| Tests & e2e prereqs | `docs/TESTING.md` |
| Styling & CSS cache bust | `docs/STYLING.md` |
| SSR contract (plain HTML + `data-*` hooks) | `docs/SSR-CONTRACT.md` |
| Isomorphic BUD (one renderer for SSR+WASM) | `docs/C-ISOMORPHIC-BUD.md` |
| WASM bridge | `docs/WASM-BRIDGE.md` |
| Filters & schema hints | `docs/FILTERS.md` / `docs/SCHEMA.md` |
| Pickers & Omni-Dropdowns | `docs/PICKERS.md` |
| Extension guide & custom Pi workflows | `docs/EXTENSIONS.md` |
| Deep audit & technical debt backlog | `docs/AUDIT.md` |
