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
   Forbidden in UX: `XY_`/`xy_`/`corm_`/`source_`/`axil_`/`stoma_`/
   `hyle_source_`/`var/` literals. Sanctioned `#include "*.c"` only
   `site_ui.c|list.c` (`scripts/check-module-boundaries.sh`).
   Check `grep -E 'corm_|source_|axil_|hyle_source|XY_' mods/*/ux/*.c` must
   be 0 and `sh scripts/check-wasm-imports.sh` must pass.
   Note: `mods/site_chrome/ux` is WASM-only (`WASM_ONLY=1`) — not a native module.

2. **UX avoids compile-time branching.** No `#if`/`#ifdef` for nodes.
   Branch on runtime `state`. Allowed only: `#ifndef *_C` guards,
   `__attribute__((import_module("env")…))` host imports,
   `site_ui.c:#ifndef __wasm__` aggregator,
   `site_page.c:#if __has_include` fallback (`C-ISOMORPHIC-BUD §3`).

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
   `external/libbud` is a pure 5-field UI binder (`bud_field_desc_t`) without
   any database/storage concepts. `external/libhyle-source` owns
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

7. **Autonomous Quest Management.** All work MUST use the Quest Journal workflow.
   - The assistant automatically creates and maintains active quests on disk (`.pi/quest/future/<qid>.md`) from user requests. The user does not need to invoke quest commands manually.
   - The quest file is your single source of truth for goals, status, and decisions. Update it proactively as you make progress and before context is compacted.
   - Sub-quests are created automatically via `quest_subquest` when tangent remarks or follow-ups arise.
   - Completed work is archived to `.pi/quest/archive/<qid>.zip` via `quest_archive`.
   - Never use ad-hoc scratchpads, `.todo` files, or try to keep the entire plan in your head.
   - **Unified Bundle Packaging**:
     ```text
     After testing or making changes:
         npm --prefix .pi/extensions/pi-quest run zip

     Send pi-quest-bundle.zip containing current code and latest run diagnostics.
     ```

8. **Test-Driven Development (TDD) & Quality Gates.**
   - **Build & Run First**: Discover how to build (`make`) and run (`make watch`) the project before editing feature code.
   - **Write Tests First**: Develop unit/integration/E2E tests (`make test`) BEFORE writing feature code.
   - **Iterative Loop**: Feature implementation $\rightarrow$ `make` $\rightarrow$ run/verify $\rightarrow$ test.
   - **Final Quality Gates**: Build completes with zero errors, code contains zero debug artifacts (no temp logs or leftover debug code), and the full test suite (`make test`) passes with zero failures.

## axil-nd (sibling module port) — build, run & test

Sibling repo **`~/axil-nd`**: NeverDark MUCK engine as a generic axil module
(`libaxil-nd.so`), port status + remaining work in `~/nd/ND_PORT.md` (what is
missing to complete the port; the old full-plan file was removed). Builds
against the **system-installed**
axil/corm/xylem (`/usr/bin/axil`, headers `/usr/include/ttypt`, libs
`/lib`+`/usr/lib`).

```sh
make                  # builds lib/libaxil-nd.so (+ lib/axil-nd.so SONAME link)
./test                # RUN the suite (test.sh). NOTE: `make test` only bakes
                      #   test.sh into a `test` artifact (mk recipe) — it does
                      #   NOT run it. Use ./test or ./test.sh.
```

Run manually (module loader appends `.so`, so pass the SONAME path):

```sh
( LD_LIBRARY_PATH=$PWD/lib axil -d -A -p 28000 -m /home/quirinpa/axil-nd/lib/axil-nd & )
```

Known issues:
- `test.sh` flakes ~1 in 4 (`FAIL: no 101 in response` — real connection gets
  `HTTP/1.1 200 OK`+COOP instead of 101). Recorded in `~/nd/ND_PORT.md`
  (Known quirks);
  first check: does `~/axil-tty/test.sh` (identical structure) flake too?
- `make` prints `find: './htdocs': No such file or directory` — harmless
  (htdocs/ is gitignored/absent, same as axil-tty).
- After debug sessions run `pgrep -x axil` to catch stray axil daemons left
  on fixed ports.

Key axil wiring (verified from axil source): `on_axil_connect` fires ONLY on
WS upgrade; raw TCP accepts use the weak `axil_accept` hook; returning <0
from `on_axil_parse` skips `cmd_parse` (which routes HTTP methods, so it must
pass through for HTTP/WS-upgrade requests). See `~/nd/ND_PORT.md` (Library
usage findings + Locked decisions).

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
| Advanced Git Recovery | `docs/ADVANCED_GIT_RECOVERY.md` |
