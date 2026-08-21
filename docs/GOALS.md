# Goals — architecture invariants

> What the architecture *wants* to be. Read `docs/OVERVIEW.md` first,
> then this file, then `docs/ARCHITECTURE.md` / `docs/DESIGN.md`.
> Every other doc assumes these invariants. `NAV.md` is the worked
> example of Goal 3; this file generalizes it.

Current departures from these goals are cataloged in
`docs/VIOLATIONS.md`. Do not copy a listed violation as precedent.

No-JS must always work. hyle stays framework-neutral. Client enhancement
is optional, additive, and framework-paired (bud↔WASM). SSR markup +
the hyle query API are the only cross-framework interface.

## 1. Module encapsulation — maximally independent

### 1.1 The goal

Each `mods/*` is a thin composition layer over the libraries. Modules
know as little about each other as possible. Adding a module, field,
or page does not require editing another module.

`core → common → source → index → {poem,song,gig,grp}` is a
convention, not a monolith. `core` enforces load order via
`mods/core/core.c:27,113` `xy_install`; every other edge must be
explicit and minimal.

### 1.2 Rules

1. **XY is the only cross-`.so` interface.** A module exposes
   capability via `XY_DECL`/`XY_IMPL` in a shared header behind
   `#ifndef MODULE_IMPL` (`docs/ARCHITECTURE.md:114`). Callers just
   call the function. Never `extern` a cross-`.so` symbol, never
   `#include "*.c"` from another module except the sanctioned
   C-isomorphic case (`docs/DESIGN.md:115` / `docs/C-ISOMORPHIC-BUD.md:204`
   `detail.c:12` including `site_ui.c`) — and even there, keep the
   boundary explicit.
2. **Static by default.** Non-static symbols are `XY_IMPL`'d API or
   documented exceptions (`docs/DESIGN.md:115`).
3. **Own your `var/<you>`; delegate the rest.** Path helpers live in
   `mods/common/common_storage.c:271` (`item_path_build_root`,
   `module_path_build`, `build_owner_path`). Never hardcode
   `"var/<mod>"` (`mods/poem/poem.c:62`, `mods/song/song.c:21`,
   `mods/gig/gig.c:25`, `mods/grp/grp.c:23`, `mods/index/index.c:354`
   all do) and never touch a sibling dataset's `var/` directory
   (`mods/gig/gig.c:1136` checking `var/grp`). `source` owns scanning
   and `var/` layout (`mods/source/source.c:544`); others call it.
4. **All row writes through hyle.** `source_update_item` /
   `source_delete_item` → `hyle_source_put`/`del` so `stoma_dirty`
   stays live (`docs/DESIGN.md:148`). Direct `write_meta_file` /
   `fopen("var/.../owner")` without the hyle path freezes FTS.
5. **Single ownership for cross-cutting concerns.** `auth` owns
   `item_check_ownership` (`mods/auth/auth.c:152`), `common_storage`
   owns path building and safe-id checks (`mods/common/common_storage.c:17`
   `is_safe_id`), `source` owns scan/query. Do not reimplement owner
   path, safe-id, or CSRF checks in a second module.
6. **No module hardcodes another's fields.** `mods/index/ux/list.c:3`
   `idx_select_fields_for` switching on `song→"title,type,author"` must
   become table-driven (`fields.h` / `bud_field_desc_t` per
   `docs/DESIGN.md:87`). Adding a field is one row in that module's
   table.

### 1.3 How we know we are failing today

- **Cyclic `xy_load`:** `mods/auth/auth.c:414` loads `index` while
  `mods/index/index.c:661` loads `auth` — no DAG; `mods/gig/gig.c:1243`
  transitively loads `index+mpfd+song+source+grp` even though `core`
  already did. `mods/core/core.c:27` warn-only hides a missing `.so`.
- **Textual `.c` includes:** `mods/song/ux/detail.c:12`
  `#include "../../common/ux/site_ui.c"` (762 lines), `mods/gig/ux/detail.c:19`
  `#include "../../common/list_fill.c"` (60 `qmap_`/`source_` calls
  marked `NEVER include from WASM TU`). The only sanctioned reuse is
  C-isomorphic renderers; everything else must be `XY_DECL`.
- **Global/local split is incomplete:** `mods/common/ux/site_chrome.c:89`
  now owns `#chrome-root`, while `mods/common/ux/site_ui.c:456` owns the
  local page tree. The corresponding `site_chrome.wasm` and multi-root
  loader do not exist, so the SSR contract is ahead of the runtime.
- **Duplicate ownership / path logic:** owner check in `auth`, `index`,
  and `source` (`mods/source/source.c:319` `getpwuid` fallback);
  `is_safe_id` bypassed by direct `snprintf("var/%s",module)` in
  `mods/index/index.c:354`.
- **`source` writing sibling datasets:** `mods/source/source.c:236`
  `mkdir(dir)` for `target->items_path/slug` during ref ensure, and
  `mods/source/source.c:454` clearing inverse refs by walking all
  datasets — generic but assumes every source is filesystem-backed.

### 1.4 Checklist before touching a module

- [ ] New cross-module function? Header with `XY_DECL`, impl with
      `XY_IMPL`, shared constants *outside* the `#ifndef MODULE_IMPL`
      guard.
- [ ] No `#include "*.c"` from another module (except `ux/detail.c`
      including a WASM-safe `site_ui*.c`/`list.c` pair).
- [ ] No `"var/` literal outside `common_storage` / `source`; use
      `item_path_build_root` / `module_path_build`.
- [ ] Writes go through `source_update_item`/`source_delete_item`.
- [ ] Did not add a `switch(module)` in `index`/`common` to handle a
      per-module field.

## 2. External libs — maximally independent

### 2.1 The goal

Libraries are independent, small-surface, and do not know about the
site or each other except documented deps.

```
qmap            bottom leaf
stoma  → qmap   fold only
hyle   → stoma+qmap
hyle-bud → hyle+bud+qmap   ONLY bridge that may depend on bud
bud             depends on nothing
libxylem → qmap+qsys
axil     → xylem+qmap+qsys+ssl
site mods → assemble axil+XY+hyle(+hyle-bud)+bud
```

Verify with `grep -r bud external/hyle/src include/hyle` must be `0`
before every commit (`docs/ARCHITECTURE.md:64`).

### 2.2 Rules

- `external/hyle/src` + `include/hyle` contain **no** `bud`/`lx_`/`bud_`
  symbols. Only `external/hyle/c/libhyle-bud` may include
  `bud/bud.h` (`external/hyle/c/libhyle-bud/include/hyle-bud/hyle-bud.h:4`).
  It links `LDLIBS = -lhyle -lbud -lqmap` (`external/hyle/c/libhyle-bud/Makefile:5`) and nothing else.
- `external/bud` contains **no** `qmap_`/`hyle`/`xy_`/`stoma`/`axil`
  includes (`external/bud/src/libbud.c:1`, `bud_wasm_app.c:1` only
  `bud.h`/`bud_app.h`).
- `stoma` exposes `stoma_fold` as pure `string.h`/`ctype.h`
  (`external/stoma/src/token.c:11`); its `libstoma` TU may use `qmap`
  but `token.c` does not.
- Site modules that need `hyle-bud` rendering (`gig`, `grp`, `index`)
  declare it explicitly:
  `EXTRA_CFLAGS += -I$(REPO_ROOT)/external/hyle/c/libhyle-bud/include`
  and `EXTRA_LDLIBS += -lhyle-bud` (`mods/index/Makefile:4`,
  `mods/gig/Makefile:4`). Every other module must fail to
  `#include <hyle-bud/hyle-bud.h>` — that failure is the guard.
- No `hyle` crank should need `bud.h`. `mods/source/source.c:18`
  including `bud/bud.h` for `bud_field_desc_t` converters is a layer
  blur; converters belong in `common` or `hyle-bud`.

### 2.3 How we know we are failing today

- **`build.mk:23` leaks the bridge header globally:**
  `CFLAGS += -I$(REPO_ROOT)/external/hyle/c/libhyle-bud/include` makes
  every `mods/*.so` resolve `hyle-bud.h` even when its `Makefile`
  does not declare the dep. Same leak in `Makefile:74` `lint` line.
  This defeats `AGENTS.md:38`. Fix: remove that `-I` from `build.mk:23`
  and keep it only as `EXTRA_CFLAGS` in `gig`/`grp`/`index`
  (lint must mirror the same scoping).
- **WASM bundling duplicates `hyle-bud` sources:** `mods/index/Makefile:11`
  and `mods/gig/Makefile:10` compile `external/hyle/c/libhyle-bud/src/filter.c`
  + `table.c` *into* each `.wasm` instead of linking a WASM `libhyle-bud.a`.
  Drift risk; duplicates `WASM_COMMON_SRC` (`build.mk:39`).

## 3. WASM enhancements — easy to code, smart bundling

### 3.1 Easy to code (one renderer, two builds)

Same pattern as `docs/C-ISOMORPHIC-BUD.md:1` and `mods/song/ux/detail.c:10`
→ `htdocs/song_detail.wasm` and `mods/index/ux/list_fe.c:1` →
`htdocs/list.wasm`:

1. `fields.h` defines `app_state_t` + `bud_field_desc_t[]`.
2. One pure renderer `bud_app_render()` over that state, wrapped in
   `<div id="bud-root">` (or `#chrome-root` for global) — only `bud.h`,
   `bud_jsx.h`, `bud_app.h` + pure C.
3. `wasm_init(json,len)` fills state via `bud_state_apply` / `bud_json_*`;
   handlers via `lx_bind`; patches via `bud_patch_*`.
4. Native handler builds the same state from `source`/`qmap`, serializes
   `bud-state` JSON into `extra_head`, and responds via
   `site_ui_respond_page` / `site_ui_page`.

Hard constraint: a dual-compiled TU must **not** reference `axil`,
`source`, `qmap`, `stoma`, or any `XY_DECL` (`docs/C-ISOMORPHIC-BUD.md:28`).
`--allow-undefined` in `build.mk:38` hides violations as `0x0` at runtime.

Keep native-only data collection in a separate file (`mods/index/index.c`
vs `mods/index/ux/list.c`) so the WASM TU stays pure.

### 3.2 Smart bundling — global vs page-local is automatic

**Goal from `NAV.md:5.8,6.1`:** dev names only the *page-local* module;
global chrome is automatic.

```
global chrome  site_chrome.wasm  ↔  #chrome-root + #chrome-state   (every site_ui_page)
page-local     list.wasm etc.    ↔  #bud-root    + #bud-state      (route opts in)
```

| What | Bundle | Root | State element | When |
|------|--------|------|---------------|------|
| Global nav bar, hide-on-scroll, menu toggle | `htdocs/site_chrome.wasm` | `#chrome-root` | `#chrome-state` | Every `site_ui_page` |
| List filters, song transpose, gig media | `htdocs/list.wasm`, `htdocs/song_detail.wasm`, `htdocs/gig_detail.wasm` | `#bud-root` | `#bud-state` | Route that passes `local_module` |

- `site_ui_page` (`mods/common/ux/site_ui.c:690`) is responsible for:
  rendering the chrome tree (`site_ui_chrome`), serializing `chrome-state`
  JSON with escaped `<`/`/`/control chars, emitting
  `data-modules="site_chrome"` alone or `data-modules="site_chrome <local>"`,
  and always loading `bud-client.js`. Route code stays
  `site_ui_respond_page(fd,title,page_state_json,local_module,page_tree)`
  (`NAV.md:486`).
- `htdocs/bud-client.js:6` today only handles the first token + one
  `#bud-root` and `htdocs/bud-hydrate.js:1` only parses `event`/`event:1`.
  The smart system needs: parallel `fetch` per `data-modules` token,
  per-bridge `BudWasmBridge` + `BudPatchApplier` scoped to its root,
  `#chrome-state` fed only to `site_chrome.wasm` (`NAV.md:1037`),
  `Promise.allSettled` so one 404 does not block the other
  (`NAV.md:1099`), `window.__bud_bridges` map plus `__bud_bridge` alias,
  `event@window` parsing (`scroll@window`), `window.addEventListener`
  with `{passive:true}` and `requestAnimationFrame` throttle, and
  `window.scrollY` payload (`NAV.md:1158`).
- Roots are independent: numeric `data-bud-id` may overlap between
  bridges; each bridge builds its own hydration map from its root and
  dispatches into its own runtime (`NAV.md:8.2`).

**No-JS stays sticky, not hiding.** Hide-on-scroll is enrichment only;
the bar is always visible without WASM (`NAV.md:1.1`). Patch ownership
is strict: global handler patches only `#chrome-root` nodes.

### 3.3 Build — WASM concerns must not leak into native

- `build.mk` must stay reusable: per-module Makefiles declare
  `WASM_TARGETS`, `<name>-src`, `<name>-cflags` before
  `include ../../build.mk` (`mods/song/Makefile:8`, `docs/C-ISOMORPHIC-BUD.md:194`).
  For a WASM-only asset (global chrome), support `WASM_ONLY=1`
  so `mods/site_chrome` or `mods/common` does not need a dummy `.so`
  (`NAV.md:944`). Prefer reusing the generic
  `$(WASM_PATH)/%.wasm: $($*-src) $(WASM_COMMON_SRC)` rule; do not
  copy it.
- Keep WASM-safe vs native-only files split. `site_ui.c` and
  `mods/index/ux/list.c` are WASM-compilable; `mods/common/list_fill.c`
  and `mods/source/source.c` are native-only (`docs/DESIGN.md:118`).
  `mods/gig/ux/detail.c:19` textually includes `list_fill.c`; its body is
  currently excluded under `__wasm__`, but the native cross-module
  coupling remains and must not be copied.
- Include-source reuse (`detail.c:12` `#include "../../common/ux/site_ui.c"`)
  is sanctioned for id-alignment (`docs/DESIGN.md:124`) but must stay
  minimal. Global chrome is now split into `site_chrome.c`; page-layout
  helpers still arrive through the much larger `site_ui.c` inclusion.
- Cache bust is build-owned: `SITE_CSS_V` and `SITE_CLIENT_V`
  (`mods/common/ux/site_ui.c:761-762`) must be generated by the build,
  not hand-edited (`docs/BUILD.md:57`, `NAV.md:837`).

### 3.4 How we know we are failing today

- **Single-module loader:** `htdocs/bud-client.js:10` `mod.split(/\s+/)[0]`
  + `htdocs/bud-client.js:12` `#bud-root` only + `htdocs/bud-client.js:36`
  `#bud-state` only + `htdocs/bud-client.js:53` singular
  `window.__bud_bridge`. `data-modules="site_chrome list"` would drop
  `list` (`NAV.md:181`).
- **Half-migrated global contract:** `mods/common/ux/site_ui.c:787`
  now emits `data-modules="site_chrome <local>"` on every page, but the
  single-module loader takes only `site_chrome`, uses `#bud-root`, and
  fetches a `site_chrome.wasm` that does not exist. Local WASM is skipped.
- **No global artifact:** `htdocs/*.wasm` today is `bud_demo`, `gig_detail`,
  `list`, `song_detail` only; no `site_chrome.wasm`, no
  `mods/site_chrome/Makefile`, and `mods/common/Makefile:1` has no
  `WASM_TARGETS`. `build.mk:34` silently skips missing WASI SDK.
- **Stale-dep invisibility:** `build.mk:44` lists `$($*-src)` but not the
  `#include`d `site_ui.c`; editing shared chrome does not rebuild local
  WASMs unless forced (`docs/BUILD.md:40` `rm -f htdocs/<t>.wasm && make`).
- **Size / event gaps:** no `@window` scroll dispatch
  (`htdocs/bud-hydrate.js:1,284`), no rAF throttle, no `scrollY` payload
  — blocks `NAV.md:6.1` hide-on-scroll through bud.

## 4. Dev ergonomics — custom SSR + WASM without too much trouble

The three goals above imply a fourth: a dev can ship a custom page
(both SSR and WASM) without learning the whole tree.

- **Adding a field:** one row in `fields.h` (`bud_field_desc_t` drives
  `source_def_to_qmap`, meta I/O, and `bud_state_apply`
  per `docs/DESIGN.md:87`).
- **Adding a page WASM:** one `ux/*.c` that includes only WASM-safe
  headers, defines `wasm_init` + `bud_app_render`, plus 3 lines in the
  module `Makefile` (`WASM_TARGETS`, `<name>-src`, `<name>-cflags`).
  No manual `data-modules` string, no manual `extra_head` JSON splice
  (helpers should produce it), no manual `#bud-root` wrapper.
- **Adding a module:** `source_setup` + `index_open` + optional
  `register_standard_item_handlers` with a struct-of-hooks
  (`docs/DESIGN.md:66`) — null = default. `site_ui_page` auto-injects
  global chrome/state.
- **Current friction to reduce:** `site_ui_layout` (6 args) +
  `site_ui_page` (4 args) + `site_ui_respond_page` (5 args) manual chain
  with magic `module` string (`"song_detail"`→`"/song_detail.wasm"`,
  typo = silent 404 at `htdocs/bud-client.js:17`), per-handler
  `state_buf[16384]` + `snprintf("<script id=\"bud-state\">")`
  (`mods/song/song.c:365`, `mods/gig/gig.c:410`, `mods/index/index.c:186`),
  and manual `SITE_CSS_V` bump. The fix is helpers (`NAV.md:7.1.6`
  `site_ui_page` auto `site_chrome` prefix, a `respond_with_state`
  wrapper, a `wrap_bud_root` helper) rather than new concepts.

## 5. Guardrails (check before commit)

1. `grep -rn bud external/hyle/src include/hyle` must be empty
   (`docs/ARCHITECTURE.md:64`). Only `external/hyle/c/libhyle-bud` may
   mention `bud`.
2. New `WASM` TU: `grep -E 'qmap_|source_|axil_|XY_' ux/<your>.c` must be
   empty; any hit will be `--allow-undefined` at `build.mk:38` and crash
   in the browser only.
3. New cross-module symbol: `XY_DECL` in header, `XY_IMPL` in owner,
   shared constants outside `#ifndef MODULE_IMPL` (`docs/CONVENTIONS.md:72`).
4. New field: one row in `fields.h`; no `switch(module)` in `index`.
5. New write path: through `source_update_item`/`source_delete_item` only.
6. `build.mk` `lint`/`format` use the same include set as the build;
   do not add a global `-I` to make a TU compile — add `EXTRA_CFLAGS`
   in that module's `Makefile`.
7. Manual verification still required: `rm -f htdocs/<t>.wasm && make`,
   restart `axil -C . -p 8080 -d -m mods/core/core`, and check
   `#bud-root`/`data-bud-id` and `data-wasm-loaded` (`docs/C-ISOMORPHIC-BUD.md:216`).

## 6. How this file relates to the others

- `NAV.md` is the feature plan that **motivated** Goal 3. It defines the
  two-root (`#chrome-root` + `#bud-root`) split, `body:has()` menu
  state, and window-event bridge extension in detail. `GOALS` states the
  invariants; `NAV` shows how they apply to the sticky chrome.
- `docs/AUDIT.md` tracks security/correctness debt that also violates
  these goals (e.g. `F12` CSS `?v=` manual, `D18` `--allow-undefined`).
  Fix GOALS first, then AUDIT items stop recurring.
- `docs/ARCHITECTURE.md` is the deployment truth (load order, XY
  contract, data invariants). `docs/DESIGN.md` is the philosophy
  (evoke, not reimplement). `GOALS` is the checklist that keeps both
  true.

## 7. Related docs

- `docs/OVERVIEW.md` — orientation + repo layout
- `docs/ARCHITECTURE.md` — load order, XY, framework-pair model
- `docs/DESIGN.md` — encapsulation, minimal caller complexity
- `docs/C-ISOMORPHIC-BUD.md` — dual-compile contract, id alignment
- `docs/WASM-BRIDGE.md` — bridge/patch pitfalls
- `docs/SSR-CONTRACT.md` — no-JS markup contract
- `docs/BUILD.md` — wasm rebuild trap, stale headers, cache bust
- `docs/VIOLATIONS.md` — current violations, exceptions, and stale claims
