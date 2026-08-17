# Conventions — C style, handlers, form parsing, memory, XY

The coding conventions and request-handler patterns. `clang-format` +
`clang-tidy` enforce the mechanical parts (`make lint` / `make format`, see
`docs/BUILD.md`).

## C style (enforced)

- **Tabs** for indentation. One tab per level. No spaces.
- **Maximum 4 levels** of indentation.
- `snake_case` functions/vars, `UPPER_CASE` macros.
- **C89 style:** declare all local variables at the top of their block, before
  any statements. Never mid-block after a statement.
- System includes first, then local includes.
- `ColumnLimit: 80`, `BinPackParameters: true`, `BinPackArguments: true`.

## Handler patterns

### `with_item_access` (auth.h)

```c
static int handle_edit_get_authorized(int fd, char *body,
                                      const item_ctx_t *ctx, void *user)
{
    // ctx->fd, ctx->username, ctx->id, ctx->item_path, ctx->doc_root
    ...
}

return with_item_access(fd, body, ITEMS_PATH,
    ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
    "Item not found", "Forbidden",
    handle_edit_get_authorized, NULL);
```

Flags: `ICTX_NEED_LOGIN`, `ICTX_NEED_OWNERSHIP`, `ICTX_SONG_ID`,
`ICTX_CSRF_MPFD`, `ICTX_CSRF_QUERY`.

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
Full contract in `docs/ARCHITECTURE.md` §5. Gotchas:

- Shared header declares behind `#ifndef MODULE_IMPL`: `XY_DECL(int, my_func,
  const char *, arg);` → static inline wrapper.
- Owning `.c` (after `#define MODULE_IMPL`): `XY_IMPL(...)` then the
  definition.
- Constants BOTH sides need (e.g. `TPARAM_*`, `ICTX_*`, `SOURCE_FLAG_*`) go
  **outside** the `#ifndef MODULE_IMPL` guard — inside it, callers would never
  see them.
- Keep the exported set minimal: `static` by default.

## Data-layer invariants (write path)

- Route ALL site row writes through hyle `put`/`del`
  (`mods/source` `source_update_item`/`source_delete_item`). Writing rows
  directly into the shared qmaps bypasses `stoma_dirty` and freezes the FTS
  index. Filter/search semantics in `docs/FILTERS.md`.

## Misc pitfalls

- Do not commit `*.so`, `*.o`, `*.wasm`, swap files, or Rust `target/`.
- Include-source reuse is sanctioned for C-isomorphic units (see
  `docs/C-ISOMORPHIC-BUD.md`).

## Related docs

- `docs/ARCHITECTURE.md` — module graph, load order, XY contract.
- `docs/DESIGN.md` — why these abstractions exist (encapsulation rules).
