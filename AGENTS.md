# Site

Pure-C music site (poem, song, songbook, choir) on top of the axil HTTP library,
the libxylem XY module system, and the bud HTML builder.

---

## Quick Start

```bash
make              # build everything
AUTH_SKIP_CONFIRM=1 make watch   # auto-rebuild + restart on :8080
make unit-tests   # axil must be running on :8080
make pages-test   # 7 page-render smoke tests
make e2e-tests    # Playwright via Deno — needs server running
make lint         # clang-tidy (max 4 indent levels enforced)
make format       # clang-format on all .c/.h
```

Server self-serves from the repo root as a chroot. Required data dirs are
`items/poem/items`, `items/song/items`, `items/songbook/items`, `items/choir/items`.

## Architecture

**Runtime:**
- `axil` on port `8080` handles HTTP, auth, sessions, uploads, and business logic.
- All request handlers are C functions compiled into `mods/*/*.so`.
- `htdocs/song-client.js` loads a Rust/WASM module for song detail browser enhancements.
- `mods/ssr/` contains a Rust SSR module (Dioxus) — minor, secondary path.
- No Deno/Fresh proxy in the request path.

**Module load order (enforced by `mods/core/core.c`):**

```
axil
 ├── common.so    ~980 lines: JSON builders, response helpers, storage, str_trim
 ├── ssr.so       (Rust SSR)
 ├── source.so    (dataset CRUD, /api/dataset/*)
 ├── mods.load    (poem, songbook, bud_demo, song, choir)
 └── Others: auth, index, mpfd, redir
```

Modules are loaded via `xy_load()` → `dlopen(RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE)`.
**RTLD_LOCAL** means symbols in one `.so` are NOT visible to others — all cross-module
calls MUST go through the XY dispatch mechanism, never plain `extern`.

## XY Cross-.so Call Convention

Every public function exposed to other modules needs:

1. **Declaration in a shared header** (`#ifndef MODULE_IMPL` guard):
   ```c
   XY_DECL(int, my_func, const char *, arg);
   ```
   This expands to a static inline wrapper that dispatches through `xy_call`.

2. **Implementation in the owning `.c` file** (after `#define MODULE_IMPL`):
   ```c
   XY_IMPL(int, my_func, const char *, arg);
   int my_func(const char *arg) { ... }
   ```
   `XY_IMPL` expands to a non-static function definition PLUS an
   `__attribute__((visibility("default")))` adapter that is auto-registered via
   `AUTO_INIT`. The function itself ends up in the dynamic symbol table.

**Gotcha:** Shared headers with `XY_DECL` use a `#ifndef MODULE_IMPL` guard so
the implementing `.c` file doesn't see the `XY_DECL` inline (which would clash with
the `XY_IMPL` definition). The TPARAM_* flag constants in `common.h` must live
**outside** this guard since both callers AND the common implementation need them.

**Module headers used in this repo:**
- `mods/common/common.h` → guard `#ifndef COMMON_IMPL`
- `mods/auth/auth.h` → guard `#ifndef ITEM_IMPL`

Standard handler registration: `register_standard_item_handlers()` in common.

## Handler Patterns

### `with_item_access` (auth.h)

Simplest way to write a CRUD handler. Handles auth, item lookup, and ownership:

```c
static int handle_sb_edit_get_authorized(int fd, char *body,
                                         const item_ctx_t *ctx, void *user)
{
    // ctx->fd, ctx->username, ctx->id, ctx->item_path, ctx->doc_root
    ...
}

return with_item_access(fd, body, SONGBOOK_ITEMS_PATH,
    ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
    "Songbook not found", "Forbidden",
    handle_sb_edit_get_authorized, NULL);
```

Flags: `ICTX_NEED_LOGIN`, `ICTX_NEED_OWNERSHIP`, `ICTX_SONG_ID`,
`ICTX_CSRF_MPFD`, `ICTX_CSRF_QUERY`.

### Redirect

```c
axil_header_set(fd, "Location", location);
axil_respond(fd, 303, "");
return 0;
```

Every 303 needs `Connection: close` + `DF_TO_CLOSE` before `axil_head` if headers
haven't been sent yet.

## C Conventions (enforced by `.clang-format` + `clang-tidy`)

- **Tabs** for indentation. One tab per level. No spaces.
- **Maximum 4 levels** of indentation. Run `make lint`.
- `snake_case` for functions/vars, `UPPER_CASE` for macros.
- **C89 style:** declare all local variables at the top of their block, before
  any statements. Never mid-block after a statement.
- System includes first, then local includes.
- `BinPackParameters: true` / `BinPackArguments: true` (args pack across fewer lines).
- `ColumnLimit: 80` still enforced.
- Run `make format` after edits.

### Form parsing

- `axil_query_parse` + `axil_query_param` for `application/x-www-form-urlencoded`
- `call_mpfd_parse` + `call_mpfd_get` for `multipart/form-data` only
- GET handlers must call `axil_query_parse()` with `QUERY_STRING` env var before
  `axil_query_param()`. The `query_db` is emptied every request.

### Memory

- Never `free()` qmap-managed values.
- `qmap_put(map, key, ptr)` takes ownership of `ptr`.
- `QM_REFERENCE` field values are string IDs (slugs), not positions.

## Testing

| Command | What |
|---------|------|
| `make unit-tests` | Runs `test.sh` in each module directory. Needs axil on :8080. |
| `make pages-test` | 7-page HTTP smoke test (curls against :8080 for DOCTYPE). |
| `make e2e-tests` | Playwright via Deno. Needs server + `AUTH_SKIP_CONFIRM=1`. |
| `deno test --allow-all tests/e2e/song-add.test.ts` | Single e2e file. |

**Pre-existing failure:** `songbook` unit-test step 6 (`data.txt non-empty... FAIL: empty`)
fails even on clean `main`. Not caused by any local change.

**e2e prerequisites:** Server running with `AUTH_SKIP_CONFIRM=1` set at startup
(not just at test time). The auth module reads this on boot.

## Debug Logging

All logs captured to `debug/`:
```
debug/builds/    make output with timestamps
debug/runtime/   axil.log (via make watch)
debug/tests/     e2e output with timestamps
```

```bash
make debug-logs            # recent logs tail
make build-capture         # rebuild + save log
make test-capture          # run e2e + save
make test-single-capture TEST=foo.ts
fprintf(stderr, "DEBUG: %s\n", val);   # C debug — check debug/runtime/axil.log
```

## Chroot Setup

The site runs inside a chroot at repo root. Auth module creates `./etc/` files
automatically on first start, but `sh` + its shared libs must exist:

```bash
mkdir -p ./bin ./lib ./lib/x86_64-linux-gnu
cp /bin/sh ./bin/sh
# ldd /bin/sh → copy the libs it needs, typically libnss_files.so.2
```

## Common Pitfalls

1. Every 303 needs `Connection: close` + `DF_TO_CLOSE` before `axil_head`.
2. Do not use `call_mpfd_parse` for url-encoded forms.
3. Do not `free()` qmap-managed memory.
4. `axil_register_handler` is last-registration-wins.
5. Do not commit `*.so`, `*.o`, swap files, or Rust `target/`.
6. C frontend changes need module rebuild + server restart.
7. `-m mods/core/core` flag is required to start axil — without it, no handlers register.
8. Shared defines (`TPARAM_*`, etc.) needed by both the implementation and callers
   must go outside the `#ifndef MODULE_IMPL` guard in the header.
9. With `RTLD_LOCAL`, cross-.so calls require XY dispatch. A plain function in
   `common.so` whose address is taken by XY's adapter will be exported as `T` in
   `nm -D` (because `XY_IMPL` gives default visibility), but it is still
   **not** directly callable by name from another `.so` at link time.
10. `stoma_fold` forces a UTF-8 `LC_CTYPE` lazily (`token.c`), so accent folding
    is stable under any inherited locale — do NOT remove that, and do NOT
    replace TRANSLIT with hand-rolled accent tables. FTS tests must not pin
    `LC_ALL` (that masked a root-server C-locale bug); the fold suites are
    verified under `LC_ALL=C` too.
