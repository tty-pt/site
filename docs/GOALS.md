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
convention, not a monolith. `core` loads only foundations (`common` +
`source`); every other edge is explicit via `xy_load()` in the module's
own `xy_install()` — maximally independent means an explicit DAG, not
zero edges.

### 1.2 Rules

1. **XY is the only cross-`.so` interface.** A module exposes
   capability via `XY_DECL`/`XY_IMPL` in a shared header behind
   `#ifndef MODULE_IMPL` (`ARCHITECTURE.md:§5`). Callers just
   call the function. Never `extern` a cross-`.so` symbol, never
   `#include "*.c"` from another module except the sanctioned
   C-isomorphic case (`CONVENTIONS` / `C-ISOMORPHIC-BUD.md:§6`
   `detail.c` including `site_ui.c`) — and even there, keep the
   boundary explicit.
2. **Static by default.** Non-static symbols are `XY_IMPL`'d API or
   documented exceptions (`DESIGN.md:§5`).
3. **Own your `var/<you>`; delegate the rest.** Path helpers live in
   `mods/common/common_storage.c` (`item_path_build_root`,
   `module_path_build`, `build_owner_path`). Route handlers pass module names
   to `with_module_item_access`; they never hardcode `"var/<mod>"` or touch a
   sibling dataset's directory. `source` owns scanning and persistence layout;
   others call it.
4. **All row writes through hyle.** `source_update_item` /
   `source_delete_item` → `hyle_source_put`/`del` so `stoma_dirty`
   stays live. Direct `write_meta_file` /
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
qmap                    bottom leaf
stoma          → qmap   fold only
hyle           → stoma+qmap (canonical schema, query, FTS)
libhyle-source → hyle+qmap+stoma+json-c (persistence, store drivers, DSV, JSON)
hyle-bud       → hyle+bud+qmap   ONLY bridge that may depend on bud
bud                     pure DOM/JSX/WASM, 5-field UI binder, depends on nothing
libxylem       → qmap+qsys
axil           → xylem+qmap+qsys+ssl
site mods      → assemble axil+XY+hyle(+libhyle-source+libhyle-bud)+bud
```

Verify with `grep -r bud external/hyle/src include/hyle` must be `0`
before every commit (`ARCHITECTURE.md:§2`). `external/bud/include` and `external/bud/src`
must contain `0` database/storage includes.

### 2.2 Rules

- `external/hyle/src` + `include/hyle` contain **no** `bud`/`lx_`/`bud_`
  symbols. `external/hyle/include/hyle/schema.h` defines canonical data schemas
  (`hyle_schema_desc_t`) independently of any UI renderer.
- `external/bud` contains **no** `qmap_`/`hyle`/`xy_`/`stoma`/`axil`
  includes (`external/bud/src/libbud.c` only `bud.h`/`bud_app.h`). `bud_field_desc_t`
  is a pure 5-field UI state binder (`key`, `offset`, `size`, `is_int`, `kind`).
- Only `external/hyle/c/libhyle-bud` may include `bud/bud.h`
  (`external/hyle/c/libhyle-bud/include/hyle-bud/hyle-bud.h`). It links
  `LDLIBS = -lhyle -lbud -lqmap` and nothing else.
- `external/hyle/c/libhyle-source` is the standalone persistence engine, linking
  `-lhyle -lqmap -lstoma -ljson-c`.
- `stoma` exposes `stoma_fold` as pure `string.h`/`ctype.h`
  (`external/stoma/src/token.c:11`); its `libstoma` TU may use `qmap`
  but `token.c` does not.
- Site modules that need `hyle-bud` rendering (`gig`, `grp`, `index`)
  declare it explicitly:
  `EXTRA_CFLAGS += -I$(REPO_ROOT)/external/hyle/c/libhyle-bud/include`
  and `EXTRA_LDLIBS += -lhyle-bud`.
  `hyle-bud` is pure `bud` + C (`filter.c`, `table.c` via `hyle-bud-wasm.mk`);
  its use in `mods/*/ux` for filters/tables **is** the sanctioned UX
  bridge (see `CONVENTIONS` WASM purity).
  Note: `grp` uses `hyle-bud` for native SSR only (no `WASM_TARGETS` in its
  Makefile); `index` and `gig` also produce WASM bundles.

## 3. WASM enhancements — easy to code, smart bundling

### 3.1 Easy to code (one renderer, two builds)

Same pattern as `C-ISOMORPHIC-BUD.md:§1` and `mods/song/ux/detail.c`
→ `htdocs/song_detail.wasm` and `mods/index/ux/list_fe.c` →
`htdocs/list.wasm`: `fields.h` defines `app_state_t` + `bud_field_desc_t[]`;
one pure renderer `bud_app_render()` over that state wrapped in `#bud-root`
(`#chrome-root` for global); `wasm_init` fills state via `bud_state_apply`;
native handler builds same state from `source`/`qmap` and responds via
`site_ui_respond_page`. See `C-ISOMORPHIC-BUD.md` for hard constraints.

Hard constraint: a dual-compiled TU must **not** reference `axil`,
`source`, `qmap`, `stoma`, or any `XY_DECL`. `hyle-bud` (`hyle_bud_*`) **is**
allowed — it is the filter/table primitive compiled via `HYLE_BUD_WASM_SRC`.
`--allow-undefined` in `build.mk` hides violations as `0x0` at runtime. Keep
native-only data collection in a separate file (`mods/index/index.c`
vs `mods/index/ux/list.c`) so the WASM TU stays pure.

### 3.2 Smart bundling — global vs page-local is automatic

```
global chrome  site_chrome.wasm  ↔  #chrome-root + #chrome-state   (every site_ui_page)
page-local     list.wasm etc.    ↔  #bud-root    + #bud-state      (route opts in)
```

| What | Bundle | Root | State element | When |
|------|--------|------|---------------|------|
| Global nav bar, hide-on-scroll, menu toggle | `htdocs/site_chrome.wasm` | `#chrome-root` | `#chrome-state` | Every `site_ui_page` |
| List filters, song transpose, gig media | `htdocs/list.wasm`, `htdocs/song_detail.wasm`, `htdocs/gig_detail.wasm` | `#bud-root` | `#bud-state` | Route that passes `local_module` |

- `site_ui_page` is responsible for rendering the chrome tree, serializing
  `chrome-state` JSON, emitting `data-modules="site_chrome"` alone or
  `data-modules="site_chrome <local>"`, and loading `bud-client.js`. Route
  code stays `site_ui_respond_page(fd,title,page_state_json,local_module,page_tree)`.
- `htdocs/bud-client.js` handles parallel `fetch` per `data-modules` token,
  per-bridge scoping, `Promise.allSettled`, `event@window`, `window.scrollY`.
- **No-JS stays sticky, not hiding.** Hide-on-scroll is enrichment only.

### 3.3 Build — WASM concerns must not leak into native

- `build.mk` stays reusable: per-module Makefiles declare `WASM_TARGETS`, `<name>-src`, `<name>-cflags` before `include ../../build.mk` (`C-ISOMORPHIC-BUD.md:§6`). `WASM_ONLY=1` supported so `mods/site_chrome` has no dummy `.so`.
- WASM-safe vs native-only split holds: `site_ui.c` + `mods/index/ux/list.c` WASM-compilable; `mods/common/list_fill.c` + `mods/source/source.c` native-only. Sanctioned includes limited to pure C-isomorphic `mods/common/ux/site_ui.c`/`mods/index/ux/list.c`/`mods/song/ux/music.c`.
- Include-source reuse (`detail.c` `#include "../../common/ux/site_ui.c"`) sanctioned for id-alignment and is minimal: `site_ui.c` is the aggregator with native `site_chrome.c`/`site_page.c` under `#ifndef __wasm__`.
- Cache bust is build-owned: `SITE_CSS_V`/`SITE_CLIENT_V` `mods/common/ux/version.gen.h` generated by `scripts/gen-asset-version.sh` via `__has_include` fallback; no hand edit (`BUILD.md`).

## 4. Dev ergonomics — custom SSR + WASM without too much trouble

- **Adding a field:** one row in `fields.h` (`bud_field_desc_t` drives
  `source_def_to_qmap`, meta I/O, and `bud_state_apply`).
- **Adding a page WASM:** one `ux/*.c` that includes only WASM-safe
  headers, defines `wasm_init` + `bud_app_render`, plus 3 lines in the
  module `Makefile` (`WASM_TARGETS`, `<name>-src`, `<name>-cflags`).
- **Adding a module:** `source_setup` + `index_open` + optional
  `register_standard_item_handlers` with a struct-of-hooks — null = default.
  `site_ui_page` auto-injects global chrome/state.
- **Helpers:** `site_ui_layout`/`site_ui_page`/`site_ui_respond_page` owning
  `<script id="bud-state">` escape + chrome/state plumbing; handlers supply
  `state_json+module`.

## 5. Guardrails (check before commit — must be 0 / pass)

1. `grep -rn bud external/hyle/src include/hyle` must be empty
    (`ARCHITECTURE.md:§2`). Only `external/hyle/c/libhyle-bud` may
    mention `bud`.
2. New `WASM` TU: `grep -E 'qmap_|source_|axil_|hyle_source|XY_' ux/<your>.c` must be
    empty; `hyle_bud_*` is the only allowed `hyle_*` in UX. Any hit will be
    `--allow-undefined` and crash in the browser only. WASM-safe files are
    `site_ui.c`/`list.c`/`music.c` (+ `hyle-bud` `filter.c`/`table.c`); `list_fill.c`/`source.c` are native-only.
3. New cross-module symbol: `XY_DECL` in header, `XY_IMPL` in owner,
    shared constants outside `#ifndef MODULE_IMPL` (`CONVENTIONS:§XY`, `ARCHITECTURE.md:§5`). Never plain `extern`.
4. No `#include "*.c"` across modules except sanctioned pure C-isomorphic `mods/common/ux/site_ui.c|mods/index/ux/list.c|mods/song/ux/music.c` (`scripts/check-module-boundaries.sh:26`). Keep `static` by default.
5. No `"var/` literal outside `common_storage.c` / `source_store_fs.c` + `source_setup` registration; use `with_module_item_access` / `item_path_build_root`.
6. New write path: through `source_update_item`/`source_delete_item` → `hyle put/del` only (`ARCHITECTURE.md:§6`). Direct `fopen("var/...")` freezes FTS.
7. New field: one row in `fields.h`; no `switch(module)` in `index` — declare `source_list_view_t` beside field table.
8. `hyle-bud` is per-module: `EXTRA_CFLAGS += -I$(REPO_ROOT)/external/hyle/c/libhyle-bud/include` + `EXTRA_LDLIBS += -lhyle-bud` in `mods/index,gig,grp/Makefile` only; `hyle-bud-wasm.mk` is single `HYLE_BUD_WASM_SRC` declaration. No global `-I` in `build.mk`.
9. `sh scripts/check-module-boundaries.sh && sh scripts/check-ux-purity.sh && sh scripts/check-wasm-imports.sh` must pass (blocking `make all:boundary-check`). `wasm-allowed-imports.lst` allowlists `env.bud_host_*` only.
10. Site-specific surface minimal (blocking): `grep -E '"(poem|song|gig|grp)"' mods/common mods/index --include="*.h" --include="*.c"` must be 0 outside `source_list_view_t` registration — per-module registration keeps `common/index` reusable.
11. Feature placement — consider owning http server: choose owner `HTTP→axil`, `dataset/query/FTS→source→hyle`, `collection/list→index+hyle-bud`, `chrome/forms→common/ux`, `domain→song/gig/grp`. If 2+ callers need it, invent in the library; if handler >30 lines, extend abstraction.
12. Manual verification still required: `make -j4`, restart `axil -C . -p 8080 -d -m mods/core/core`, and check `#bud-root`/`data-bud-id` and `data-wasm-loaded` (`C-ISOMORPHIC-BUD.md:§7`).

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
- `docs/VIOLATIONS.md` — 0 open (archived in `git log -- docs/VIOLATIONS.md`), deliberate exceptions + confirmed boundaries
