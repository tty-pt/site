# Conventions — C style, handlers, form parsing, memory, XY

The coding conventions and request-handler patterns. `clang-format` +
`clang-tidy` enforce the mechanical parts (`make lint` / `make format`, see
`docs/BUILD.md`). Encapsulation rules below are enforced by
`scripts/check-module-boundaries.sh` + `scripts/check-wasm-imports.sh`
(`make all:boundary-check`).

## C style (enforced)

- **Tabs** for indentation. One tab per level. No spaces.
- **Maximum 4 levels** of indentation.
- `snake_case` functions/vars, `UPPER_CASE` macros.
- **C89 style:** declare all local variables at the top of their block, before
  any statements. Never mid-block after a statement.
- System includes first, then local includes.
- `ColumnLimit: 80`, `BinPackParameters: true`, `BinPackArguments: true`.

## Handler patterns

### `with_module_item_access` (auth.h)

```c
static int handle_edit_get_authorized(int fd, char *body,
                                      const item_ctx_t *ctx, void *user)
{
    // ctx->fd, ctx->username, ctx->id, ctx->item_path, ctx->doc_root
    ...
}

return with_module_item_access(fd, body, "song",
    ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
    "Item not found", "Forbidden",
    handle_edit_get_authorized, NULL);
```

Flags: `ICTX_NEED_LOGIN`, `ICTX_NEED_OWNERSHIP`, `ICTX_SONG_ID`,
`ICTX_CSRF_MPFD`, `ICTX_CSRF_QUERY`.

Ownership identity comes only from auth's `item_owner_record`,
`item_owner_read`, and `item_owner_check`. Do not compare cached owner fields
for authorization and do not derive site usernames from filesystem UIDs.

### Redirect

```c
axil_header_set(fd, "Location", location);
axil_respond(fd, 303, "");
```

Every 303 needs `Connection: close` + `DF_TO_CLOSE` before `axil_head` if
headers haven't been sent yet.

### Handler registration

`register_standard_item_handlers(name, &handlers)` (struct-of-hooks, NULL =
default) and `index_open(...)` register the generic CRUD routes.
`axil_register_handler` is **last-registration-wins** — registering twice
replaces, it does not append.

## Form parsing

- `axil_query_parse` + `axil_query_param` for `application/x-www-form-urlencoded`.
- `call_mpfd_parse` + `call_mpfd_get` for `multipart/form-data` ONLY. Do not
  mix the two.
- GET handlers must call `axil_query_parse()` with `QUERY_STRING` env var
  before `axil_query_param()`. The `query_db` is emptied every request.
- `mpfd_get` contract: returns the **copied** byte count (not the field
  length) and NUL-terminates whenever there's room. Size buffers to the field
  and trust the return value as the count.

## Memory

- Never `free()` qmap-managed values.
- `qmap_put(map, key, ptr)` copies key and value; caller retains ownership.
- `QM_REFERENCE` field values are string IDs (slugs), not positions.

## XY cross-.so convention

Modules are dlopen'd `RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE`; cross-.so calls MUST
go through the XY dispatch mechanism, never plain `extern` (a default-visibility
symbol exported by `XY_IMPL` is still not callable by name from another .so).
Full contract in `docs/ARCHITECTURE.md` §5.

- Shared header declares behind `#ifndef MODULE_IMPL`: `XY_DECL(int, my_func,
  const char *, arg);` → static inline wrapper.
- Owning `.c` (after `#define MODULE_IMPL`): `XY_IMPL(...)` then the
  definition.
- Constants BOTH sides need (e.g. `TPARAM_*`, `ICTX_*`, `SOURCE_FLAG_*`) go
  **outside** the `#ifndef MODULE_IMPL` guard — inside it, callers would never
  see them.
- Keep the exported set minimal: `static` by default.
- Do not `#include "*.c"` across modules except the three sanctioned pure
  C-isomorphic files `mods/common/ux/site_ui.c|mods/index/ux/list.c|
  mods/song/ux/music.c` (`scripts/check-module-boundaries.sh:26`). Keep
  `static` by default.

## Module dependencies — declare immediate `xy_load` deps

Maximally independent does not mean zero deps — it means an explicit DAG via
`xy_load()` in each `xy_install()`. `core` loads only foundations (`common` +
`source`; `common` itself `xy_load`s `mpfd`) then `mods.load`:

```
core → common → source → index → {poem→index, song→index+mpfd,
  grp→index+mpfd+song, gig→index+mpfd+song+source+grp}
index → common, auth, mpfd; auth → common, libaxil-auth (external)
```

- `poem.c:145` `xy_load("./mods/index/index")` is the exemplar (thin, 1 dep).
- `song → index, mpfd`, `grp → index, mpfd, song`,
  `gig → index, mpfd, song, source, grp` (`ARCHITECTURE.md:§3`). `libxylem`
  deduplicates repeated loads; `mods.load` order `poem→song→grp→gig` respects
  the DAG. External deps like `libaxil-auth` are marked external.
- Do not centralize feature deps in `core.c` — each module owns its edges.

## Filesystem ownership — no `var/` literals

Own your `var/<you>`; delegate the rest. Path helpers live in
`mods/common/common_storage.c` (`item_path_build_root`, `module_path_build`,
`build_owner_path`). Use `with_module_item_access` / `item_path_build_root`;
never hardcode `"var/<mod>"` or touch a sibling dataset's directory. `source`
owns scanning and persistence layout.

Allowed only: `common_storage.c|common_storage.h`, `source/*`, and
`source_setup("…","var/<module>")` registration in `poem|song|grp|gig`
(`scripts/check-module-boundaries.sh:47`). All other `var/` literals fail the
check.

## Single ownership for cross-cutting concerns

`auth` owns `item_owner_record`/`read`/`check` (`mods/auth/auth.c`), `common_storage`
owns path building and `is_safe_id` (`mods/common/common_storage.c:17`), `source`
owns scan/query. Do not reimplement owner path, safe-id, or CSRF checks in a
second module.

## No hardcoded feature names in `common`/`index`

Do not `switch(module)` or `grep '"(poem|song|gig|grp)"'` in `common`/`index`
outside `source_list_view_t` registration (`source.h:50`,
`source.c:source_get_list_view`). Each module declares its ordered list fields
and labels beside its `fields.h`; `list_fill_state` consumes that registration.
Grandfathered `site_paths.c:68` icon table is the only allowed enumeration.

## WASM purity — UX is `bud` + `hyle-bud` + pure C

A dual-compiled UX TU (`mods/*/ux/*.c` + `mods/common/ux/*`) may only include
`bud.h`/`bud_jsx.h`/`bud_app.h` + `hyle-bud/hyle-bud.h` (for filters/tables in
`index`/`gig`/`grp`) + pure C (`string.h`, etc.). It must **not**
reference `qmap_`, `source_`, `axil_`, `stoma_`, `hyle_source_`, `XY_`/`xy_`,
or `var/` literals. Per-module WASM that needs `hyle-bud` declares
`EXTRA_CFLAGS += -I…/hyle-bud/include` + `EXTRA_LDLIBS += -lhyle-bud` and
`HYLE_BUD_WASM_SRC` (`hyle-bud-wasm.mk`). Every other module must fail to
`#include <hyle-bud/hyle-bud.h>` — that failure is the guard.

- Check: `grep -E 'qmap_|source_|axil_|hyle_source|XY_' mods/*/ux/*.c` must be
  empty (native-only `site_page.c` behind `site_ui.c:#ifndef __wasm__` is not a
  WASM TU and is excluded); `sh scripts/check-wasm-imports.sh` must pass
  (`wasm-allowed-imports.lst` allowlists only `env.bud_host_*`).

## UX avoids preprocessor conditions

No `#if`/`#ifdef` for node branching in UX renderers. Branch on runtime
`state` (`if (state.show_media)`) or split native-only logic into a separate
file outside `ux/` (`GOALS.md:§3`). Allowed only: `#ifndef *_C` include guards,
`__attribute__((import_module("env")…))` host imports,
`site_ui.c:#ifndef __wasm__` aggregator, and
`site_page.c:#if __has_include` fallback (`C-ISOMORPHIC-BUD §3` id-drift).

## Data-layer invariants (write path)

- Route ALL site row writes through hyle `put`/`del`
  (`mods/source` `source_update_item`/`source_delete_item`). Writing rows
  directly into the shared qmaps bypasses `stoma_dirty` and freezes the FTS
  index. Filter/search semantics in `docs/FILTERS.md`.
- Ordered `gig.songs`/`grp.songs` via `hyle_source_ordered_*` are sanctioned
  until DSV migration; item sources must use `source`.

## CSP headers

Security headers (CSP, nosniff, X-Frame-Options, Referrer-Policy) are set in
`mods/common/common_response.c:44-57` (`common_response_set_security_headers`)
and CSP `script-src` is extended in `mods/common/ux/site_ui.c:700-723` to
include the inline `<script id="bud-state">` hash for the current page.
`site_core_build_csp_extra` in dead `mods/site_core/` is not used.

## Misc pitfalls

- Do not commit `*.so`, `*.o`, `*.wasm`, swap files, or Rust `target/`.
- Include-source reuse is sanctioned for C-isomorphic units (see
  `docs/C-ISOMORPHIC-BUD.md`): only `site_ui.c|list.c|music.c`.
- `bud_patch_text` must target a TEXT node, not an element (`WASM-BRIDGE.md:§6`).

## Related docs

- `docs/ARCHITECTURE.md` — module graph, load order, XY contract.
- `docs/DESIGN.md` — why these abstractions exist (encapsulation rules).
