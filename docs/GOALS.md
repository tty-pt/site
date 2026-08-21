# Goals — architecture invariants

> What the architecture *wants* to be. Read `docs/OVERVIEW.md` first,
> then this file, then `docs/ARCHITECTURE.md` / `docs/DESIGN.md`.
> Every other doc assumes these invariants.

Departures from these goals are cataloged in `docs/VIOLATIONS.md` (currently 0 open). Do not introduce new violations.

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
   `mods/common/common_storage.c` (`item_path_build_root`,
   `module_path_build`, `build_owner_path`). Route handlers pass module names
   to `with_module_item_access`; they never hardcode `"var/<mod>"` or touch a
   sibling dataset's directory. `source` owns scanning and persistence layout;
   others call it.
4. **All row writes through hyle.** `source_update_item` /
   `source_delete_item` → `hyle_source_put`/`del` so `stoma_dirty`
   stays live (`docs/DESIGN.md:148`). Direct `write_meta_file` /
   `fopen("var/.../owner")` without the hyle path freezes FTS.
5. **Single ownership for cross-cutting concerns.** `auth` owns
   `item_owner_record`/`read`/`check` (`mods/auth/auth.c`), `common_storage`
   owns path building and safe-id checks (`mods/common/common_storage.c:17`
   `is_safe_id`), `source` owns scan/query. Do not reimplement owner
   path, safe-id, or CSRF checks in a second module.
6. **No module hardcodes another's fields.** Each module declares its ordered
   list fields and labels in a framework-neutral `source_list_view_t` beside
   its field table. `list_fill_state` consumes that registration without a
   feature-name switch in index.

### 1.4 Checklist before touching a module

- [ ] New cross-module function? Header with `XY_DECL`, impl with
      `XY_IMPL`, shared constants *outside* the `#ifndef MODULE_IMPL`
      guard.
- [ ] No `#include "*.c"` from another module (except `ux/detail.c`
      including a WASM-safe `site_ui*.c`/`list.c` pair).
- [ ] No `"var/` literal outside `common_storage` / `source` registration; use
      `with_module_item_access`, `item_path_build_root`, or
      `module_path_build`.
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
  `site_ui_respond_page(fd,title,page_state_json,local_module,page_tree)`.
- `htdocs/bud-client.js:6` handles parallel `fetch` per `data-modules` token
  via `moduleSpec` + per-bridge `BudWasmBridge`/`BudPatchApplier` scoped to its root,
  `#chrome-state` fed only to `site_chrome.wasm`, `Promise.allSettled` so one 404
  does not block the other, `window.__bud_bridges` + `__bud_bridge` alias,
  `event@window` parsing (`scroll@window`), `window.addEventListener`
  with `{passive:true}` and `requestAnimationFrame` throttle, and
  `window.scrollY` payload.
- Roots are independent: numeric `data-bud-id` may overlap between
  bridges; each bridge builds its own hydration map from its root and
  dispatches into its own runtime.

**No-JS stays sticky, not hiding.** Hide-on-scroll is enrichment only;
the bar is always visible without WASM. Patch ownership
is strict: global handler patches only `#chrome-root` nodes.

### 3.3 Build — WASM concerns must not leak into native

- `build.mk` stays reusable: per-module Makefiles declare `WASM_TARGETS`, `<name>-src`, `<name>-cflags` before `include ../../build.mk` (`mods/song/Makefile:8`, `docs/C-ISOMORPHIC-BUD.md:194`). `WASM_ONLY=1` supported `build.mk:42` so `mods/site_chrome` has no dummy `.so`; generic `$(WASM_PATH)/%.wasm: $($*-src) $(WASM_COMMON_SRC)` reused.
- WASM-safe vs native-only split holds: `site_ui.c` + `mods/index/ux/list.c` WASM-compilable; `mods/common/list_fill.c` + `mods/source/source.c` native-only (`docs/DESIGN.md:118`). `list_fill` lives once in `index` via `XY` (`mods/index/list_fill.c:4`); sanctioned includes limited to pure C-isomorphic `mods/common/ux/site_ui.c`/`mods/index/ux/list.c`/`mods/song/ux/music.c` `scripts/check-module-boundaries.sh:26`.
- Include-source reuse (`detail.c:12` `#include "../../common/ux/site_ui.c"`) sanctioned for id-alignment (`docs/DESIGN.md:124`) and is minimal: `site_ui.c` is 14-line aggregator `site_paths.c`/`site_layout.c`/`site_forms.c`/`site_media.c` + native `site_chrome.c`/`site_page.c` under `#ifndef __wasm__`.
- Cache bust is build-owned: `SITE_CSS_V`/`SITE_CLIENT_V` `mods/common/ux/version.gen.h` generated by `scripts/gen-asset-version.sh` cksum via `__has_include` fallback `mods/common/ux/site_page.c:108`; no hand edit `docs/BUILD.md:57`.

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
- **Helpers:** `site_ui_layout`/`site_ui_page`/`site_ui_respond_page` via `mods/common/ux/site_page.c:195` `site_ui_state_head()` + `site_ui_respond_with_state()` owning `<script id="bud-state">` escape + `site_ui_page(...,module,…)` chrome/state plumbing; handlers supply `state_json+module`.

## 5. Guardrails (check before commit — must be 0 / pass)

1. `grep -rn bud external/hyle/src include/hyle` must be empty
   (`docs/ARCHITECTURE.md:64`). Only `external/hyle/c/libhyle-bud` may
   mention `bud`.
2. New `WASM` TU: `grep -E 'qmap_|source_|axil_|XY_' ux/<your>.c` must be
   empty; any hit will be `--allow-undefined` at `build.mk:37` and crash
   in the browser only (`W06`). WASM-safe files are `site_ui.c`/`list.c`/`music.c` only; `list_fill.c`/`source.c` are native-only.
3. New cross-module symbol: `XY_DECL` in header, `XY_IMPL` in owner,
   shared constants outside `#ifndef MODULE_IMPL` (`docs/CONVENTIONS.md:72`, `docs/ARCHITECTURE.md:120`). Never plain `extern`.
4. No `#include "*.c"` across modules except sanctioned pure C-isomorphic `mods/common/ux/site_ui.c|mods/index/ux/list.c|mods/song/ux/music.c` (`scripts/check-module-boundaries.sh:26`, `M03`). Keep `static` by default.
5. No `"var/` literal outside `common_storage.c` / `source_store_fs.c` + `source_setup` registration; use `with_module_item_access` / `item_path_build_root` (`M04`).
6. New write path: through `source_update_item`/`source_delete_item` → `hyle put/del` only (`ARCHITECTURE.md:144`). Direct `fopen("var/...")` freezes FTS.
7. New field: one row in `fields.h`; no `switch(module)` in `index` — declare `source_list_view_t` beside field table (`M07`).
8. `hyle-bud` is per-module: `EXTRA_CFLAGS += -I$(REPO_ROOT)/external/hyle/c/libhyle-bud/include` + `EXTRA_LDLIBS += -lhyle-bud` in `mods/index,gig,grp/Makefile:3-4` only (`L01`); `hyle-bud-wasm.mk:1` is single `HYLE_BUD_WASM_SRC` declaration (`L03`). No global `-I` in `build.mk`.
9. `sh scripts/check-module-boundaries.sh && sh scripts/check-wasm-imports.sh` must pass (now blocking `make all:boundary-check`). `wasm-allowed-imports.lst` allowlists `env.bud_host_*` only.
10. Site-specific surface minimal (blocking): `grep -E '"(poem|song|gig|grp)"' mods/common mods/index --include="*.h" --include="*.c"` must be 0 outside `source_list_view_t` registration — per-module registration keeps `common/index` reusable within site, not a dumping ground. Adding a new module must not edit `common`.
11. Feature placement — consider owning http server: choose owner `HTTP→axil`, `dataset/query/FTS→source→hyle`, `collection/list→index+hyle-bud`, `chrome/forms→common/ux`, `domain→song/gig/grp`. If 2+ callers need it, invent in the library; if handler >30 lines, extend abstraction.
12. Manual verification still required: `make -j4`, restart `axil -C . -p 8080 -d -m mods/core/core`, and check `#bud-root`/`data-bud-id` and `data-wasm-loaded` (`docs/C-ISOMORPHIC-BUD.md:216`).

## 6. How this file relates to the others

- `docs/ARCHITECTURE.md` is the deployment truth (load order `§3`, XY
  contract `§5`, data invariants `§6`). `docs/DESIGN.md` is the philosophy
  (evoke, not reimplement). `GOALS` is the checklist that keeps both
  true.
- `docs/AUDIT.md` tracks security/correctness debt. Fix GOALS first, then
  AUDIT items stop recurring.

## 7. Related docs

- `docs/OVERVIEW.md` — orientation + repo layout
- `docs/ARCHITECTURE.md` — load order, XY, framework-pair model
- `docs/DESIGN.md` — encapsulation, minimal caller complexity
- `docs/C-ISOMORPHIC-BUD.md` — dual-compile contract, id alignment
- `docs/WASM-BRIDGE.md` — bridge/patch pitfalls
- `docs/SSR-CONTRACT.md` — no-JS markup contract
- `docs/BUILD.md` — wasm rebuild trap, stale headers, cache bust
- `docs/VIOLATIONS.md` — 0 open (archived `VIOLATIONS-ARCHIVE-2026-08-22.md`), deliberate exceptions + confirmed boundaries
