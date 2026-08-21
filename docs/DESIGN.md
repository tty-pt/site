# Design — encapsulation, abstraction, and minimal caller complexity

> Think Linus Torvalds, but for the web: small libraries with small surfaces,
> where complex features are **evoked, not reimplemented**. The goal is low
> repetition and high maintainability. Read this before designing anything.

## 1. The principle

> **The caller describes intent, not mechanism.**

A feature should be *one call away*: `register_standard_item_handlers("song",
&handlers)` registers five routes with auth, ownership, and CSRF baked in.
`with_module_item_access(...)` wraps the entire request-preamble dance in one
call without exposing storage paths.
`index_open("Song", "song.items", ...)` gives a module a full CRUD surface.
`source_setup(...)` declares a dataset and the query/update machinery just
works.

If a handler has to do 30 lines of "boilerplate" before its actual logic, the
abstraction is wrong. Push the boilerplate into the helper, not the caller.

### 1.6 Well-made abstractions & minimal site-specific surface (blocking)

- **Thin feature modules:** `poem/song/gig/grp` target ≈150 lines, `xy_install` ≈15 lines (`poem.c:160` exemplar). If handler needs 30-line boilerplate, extend the abstraction — do not copy into the module.
- **Evoke, don’t branch:** one row in `fields.h` → `source_def_to_qmap` + `META_READ` + `bud_state_apply` `§4.4`; `register_standard_item_handlers("song",&h)` + `ICTX_*` `§4.2`; `source_list_view_t` `source.h:50` consumed by `list_fill_state` `index/list_fill.c:378` — never `switch(module)` in `common/index` `GOALS.md:55` `M07`.
- **We own the http server — invent well:** prefer extending `axil` (`axil_id_from_title`, `axil_csrf_*`, `axil_static`/`axil_sendfile`) / `hyle` (`hyle_source_query` pagination `ARCHITECTURE.md:143`) / `bud` (`bud_host_*`) over shimming in `common`. `external/*` libs must stay site-agnostic (`grep -rn bud external/hyle/src==0` `GOALS.md:5(1)`).
- **Site-specific surface minimal (blocking):** `common` is reusable *within this site*, not a per-feature dumping ground. Hardcoded `site_paths.c:68` icon switch, CSP `common_response.c:51`, `TPARAM_*` `common.h:211` are site-specific smells — per-module registration (`source_list_view_t.icon` / `site_core_register_module` called from feature `xy_install`) keeps `common/index` generic. Adding a new module must not edit `common`. Enforced by `scripts/check-module-boundaries.sh` — `grep -E '"(poem|song|gig|grp)"' mods/common mods/index` must be 0 outside `source_list_view_t` registration.

**Current open surface (grandfathered, next to clean):** `site_paths.c:68` icon table, `common_response.c:51` CSP `frame-src youtube`, `common.h:211` `TPARAM_*` song-flags in `common`, `source.c` fallback `mkdir`/`slurp` duplication (`source_store_t` already isolates FS — remove fallback branches), ordered `gig.songs`/`grp.songs` via `hyle_source_ordered_*` not `source_store_dsv`, pagination via `__total__` sentinel + manual slice `index/list_fill.c:534` (hyle should return `total` structurally).

## 2. Layering: who may call whom

```
external libraries (neutral, self-contained)
   axil   HTTP primitives            libxylem  XY dispatch (dlopen hidden)
   qmap   opaque data store          stoma     tokenization/search
   hyle   data layer (NO component symbols)
   hyle-bud  the ONLY bud-dependent binding (external/hyle/c/libhyle-bud)
   bud    C DOM scaffold + WASM bridge (depends on nothing above)
site modules (thin composition)
   core → common → source → index → song/poem/gig/grp (+ auth, mpfd)
```

Rules:
- Externals never know about the site or each other (except documented deps:
  `hyle → stoma → qmap`; `hyle-bud → hyle + bud + qmap`).
- `external/hyle/src` + `include/hyle` must stay free of bud/component symbols.
  Only `libhyle-bud` may depend on bud.
- Site modules are the composition layer: they wire libraries together, keep
  their own code thin (`poem.c` ≈ 130 lines; song's `xy_install` ≈ 40 lines).
- Verify the boundary with a grep before committing.

## 3. The XY contract: cross-module calls are plain function calls

`dlopen(RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE)` is an **implementation detail**
hidden by libxylem. A module exposes capability by:

1. **Shared header** declaring it behind `#ifndef MODULE_IMPL`:
   `XY_DECL(int, my_func, const char *, arg);` — expands to a static inline
   wrapper that dispatches through `xy_call`. **Callers just call the function.**
2. **Implementer** (`#define MODULE_IMPL`, then `XY_IMPL(...)` + definition).

Consequences:
- **The header IS the API.** The implementation file is irrelevant to callers.
- Constants both sides need (e.g. `TPARAM_*`, `ICTX_*`, `SOURCE_FLAG_*`) live
  **outside** the `#ifndef` guard, so callers AND implementer see them.
- Dependencies are declared by `xy_load()` in `xy_install()` — the graph is
  explicit and load-order is enforced by `mods/core/core.c`.
- Never `extern` a cross-.so function directly; never add a new module header
  without the guard. Keep the exported set minimal: `static` by default,
  export only what others genuinely need.

## 4. Evoking complex features (the patterns that make it one-line)

### 4.1 Struct-of-hooks + NULL = default
`standard_item_handlers_t { detail, add_get, add_post, edit_get, edit_post }`;
`register_standard_item_handlers("song", &h)` registers only the non-NULL ones.
`index_open(name, dataset, cleanup, detail, add, edit_get, edit_post)` — NULL
hooks mean "no custom handler; use the generic one". A module supplies only its
novelty.

### 4.2 Flags for behavior
`with_module_item_access(fd, body, module,
ICTX_NEED_LOGIN|ICTX_NEED_OWNERSHIP|..., notfound, forbidden, cb, user)` — path
resolution + auth + lookup + error responses, one call; the handler receives a
fully-populated `item_ctx_t`. `TPARAM_*`, `SOURCE_FLAG_*` follow the same
pattern: a small flags word, not a dozen bool params.

### 4.3 Opaque handles + documented ownership
- `qmap` handles are `unsigned`; **never `free()` a qmap value**; `qmap_put`
  copies key and value; caller retains ownership.
- `hyle_query_t` documents its lifetime in the header comment (caller keeps
  `query_str` alive; must call `hyle_query_clear`). Ownership rules live in the
  declaration, next to the type — that IS the documentation.
- `bud_node *` returned by renderers is owned by the tree; renderers return
  `NULL` on "nothing here" and callers check it.

### 4.4 Data-driven tables: one source of truth
`fields.h` (`bud_field_desc_t[]` per module) drives everything:
- server field generators (`source_def_to_qmap`),
- metadata I/O (`source_meta_read/write`, `META_READ`/`META_WRITE` macros),
- WASM state (`bud_state_apply`).

Adding a field = adding a row to the table. No per-field boilerplate anywhere.

### 4.5 Shared render functions over state structs (C-isomorphic)
One renderer + one state struct, used by both native SSR and the WASM bundle
(`list_render(list_state_t*)`, `bud_app_render()`/`app_state_t`). The data path
differs (qmap vs JSON); the render code does not. See
`docs/C-ISOMORPHIC-BUD.md` — and note the id-alignment trap this pattern exists
to satisfy.

### 4.6 Centralized response/page helpers
`respond_html/json/error`, `bad_request`, `server_error`, `not_found`,
`site_ui_respond_page`, `site_ui_respond_form_page` — handlers never hand-roll
HTTP or page shells.

### 4.7 The only sanctioned write path
All row writes go through hyle `put`/`del` → `source_update_item` /
`source_delete_item`. Writing qmaps directly bypasses `stoma_dirty` and freezes
the FTS index. The abstraction is not a suggestion — it is the invariant that
keeps search live.

## 5. Encapsulation rules (how we keep the walls clean)

1. **`static` by default.** Non-static symbols are either `XY_IMPL`'d API or
   deliberately documented exceptions.
2. **Owned vs borrowed is explicit** in headers; when in doubt, state it.
3. **Split native-only from WASM-safe units.** Native data collection (qmap/
   source/axil) lives in files the wasm never compiles; pure render helpers live
   in WASM-safe files (`mods/index/ux/list.c`, `common/ux/site_ui.c`).
4. **Composition over copy.** Repeated logic in 2+ modules belongs in
   `common`/`source`/`hyle` as a helper — do not fork it.
5. **Include-source reuse is sanctioned** for C-isomorphic units
   (`ux/detail.c` `#include`s `site_ui.c`, `music.c`) — reuse without a second
   compilation unit, keeping ids aligned across native/WASM.
6. **Every module owns its tests** (`test.sh`, invoked by `make unit-tests` in
   the same way) — one command runs the whole suite.

## 6. Abstraction guardrails (do not erode)

- hyle stays framework-neutral; hints ride schema strings **opaquely**
  (`docs/SCHEMA.md`) and the component layer interprets them.
- SSR is plain HTML + `data-*` hooks; bud's `data-bud-*`/patch stream is
  bud-stack-internal and additive (`docs/SSR-CONTRACT.md`).
- Frameworks are pairs (bud↔WASM, React↔React); mixed stacks are outliers and
  get no shared bridge (`docs/ARCHITECTURE.md`).
- No-JS must always work.

## 7. Checklist: adding a feature/module

- [ ] Reuse: `index_open` + `register_standard_item_handlers` +
      `with_module_item_access` + `source_setup`/`source_query` cover 90% of it?
- [ ] If you wrote the same 10 lines twice, extract a helper instead.
- [ ] New cross-module function? Header with `#ifndef MODULE_IMPL` guard,
      `XY_DECL`/`XY_IMPL`, shared constants outside the guard.
- [ ] New field? One row in the fields table; nothing else.
- [ ] New renderer? Pure bud over a state struct, dual-compiled; native data
      collection stays in a native-only file.
- [ ] New write path? Through `source_update_item`/`source_delete_item` only.
- [ ] `make lint`, `make format`, module `test.sh`, `make unit-tests`.

## 8. Anti-patterns

- 30-line preambles in handlers that should be a
  `with_module_item_access` call.
- Writing qmap rows directly (FTS freezes).
- Native-only symbols leaking into wasm units (`--allow-undefined` hides it
  until the browser crashes).
- Growing params instead of a flags word or a small struct.
- Copied blocks of handler code between modules (song vs poem vs grp).
- New externals depending on the site or on each other's internals.
- Exporting more symbols than callers need.
