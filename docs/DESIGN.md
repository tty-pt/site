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

- **Thin feature modules:** `poem/song/gig/grp` target ≈150 lines, `xy_install` ≈15 lines (`poem.c` exemplar). If handler needs 30-line boilerplate, extend the abstraction — do not copy into the module.
- **Evoke, don’t branch:** one row in `fields.h` → `hyle_source_register_def` + `hyle_bud_state_apply` (`§4.4`); `index_module_init` + `register_standard_item_handlers("song",&h)` + `ICTX_*` (`§4.2`); `source_list_view_t` (`source.h:50`) consumed by `list_fill_state` — never `switch(module)` in `common/index` (`CONVENTIONS`).
- **We own the http server — invent well:** prefer extending `axil` / `hyle` / `bud` over shimming in `common`. Libraries must stay site-agnostic (`grep -rn bud external/hyle/src` must be 0).
- **Site-specific surface minimal (blocking):** `common` is reusable *within this site*, not a per-feature dumping ground. Adding a new module must not edit `common` — use per-module registration (`source_list_view_t`). Enforced by `scripts/check-module-boundaries.sh`.

Grandfathered surface (`site_paths.c:68` icon table, etc.) is allowlisted in `scripts/check-module-boundaries.sh`; do not add more.

## 2. Layering: who may call whom

```
external libraries (neutral, self-contained)
   axil   HTTP primitives            libxylem  XY dispatch (dlopen hidden)
   qmap   opaque data store          stoma     tokenization/search
   hyle   pure data layer (canonical hyle_schema_desc_t, NO component symbols)
   libhyle-source  dataset persistence, DSV, JSON overlays, pluggable drivers
   hyle-bud  the ONLY bud-dependent bridge (external/hyle/c/libhyle-bud)
   bud    pure C DOM scaffold, 5-field UI binder, WASM bridge (depends on nothing above)
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

### 4.1 Struct-of-hooks + NULL = default & Declarative Init
`standard_item_handlers_t { detail, add_get, add_post, edit_get, edit_post }`;
`register_standard_item_handlers("song", &h)` registers only the non-NULL ones.
`index_open(name, dataset, cleanup, detail, add, edit_get, edit_post)` — NULL
hooks mean "no custom handler; use the generic one".
`index_module_init(&(index_module_def_t){...})` packages schema declaration, dataset
setup, and standard route registration into a single call. A module supplies only its
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
`fields.h` (`hyle_schema_desc_t[]` per module, defined in `external/hyle/include/hyle/schema.h`) drives everything:
- server field generators (`hyle_source_register_def`),
- persistence file/meta attributes (`in_meta`, `file`),
- validation (`required`, `min_length`),
- declarative form generation (`site_ui_form_from_desc`),
- WASM state unpacking (`hyle_bud_state_apply`).

Adding a field = adding a row to the table. No per-field boilerplate anywhere.

### 4.5 Shared render functions over state structs (C-isomorphic)
One renderer + one state struct, used by both native SSR and the WASM bundle
(`list_render(list_state_t*)`, `bud_app_render()`/`app_state_t`). The data path
differs (qmap vs JSON); the render code does not. See
`docs/C-ISOMORPHIC-BUD.md` — and note the id-alignment trap this pattern exists
to satisfy.

### 4.6 Centralized response/page helpers
`respond_html/json/error`, `bad_request`, `server_error`, `not_found`,
`site_ui_respond_page`, `site_ui_respond_form_page`, `site_ui_respond_item_detail`,
and `respond_item_file` — handlers never hand-roll HTTP, media asset streaming,
or page shells.

### 4.7 The only sanctioned write path
All row writes go through hyle `put`/`del` → `source_update_item` /
`source_delete_item`. Storage drivers in `libhyle-source` (`store_fs`) automatically
persist item files and sync FTS indices.

### 4.8 Declarative Forms & Action Helpers
`site_ui_form_from_desc(action, cancel_href, submit_label, desc, struct_ptr, csrf_token, pv, vstr_val)`:
builds complete HTML forms from schema descriptors with automated input extraction, CSRF tokens,
and sibling GET forms for No-JS picker support.
`site_ui_action_form(action, button_label, csrf_token, hidden_inputs)`: one-line CSRF-protected action forms.
`site_ui_item_row(title, badge, action_forms, count)`: uniform child/item row builder.

### 4.9 String-First Pickers & Scoped Collection
`site_ui_picker(target_source, ...)` and `site_ui_row_replace_picker(target_source, row_idx, ...)`:
evoke searchable dropdown pickers and inline row replacers using simple dataset strings (e.g. `"song.items"`).
`pick_view_collect_desc_fd(fd, defs, &pv, &active_scope)`: collects query parameters and active scopes in one call.
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
