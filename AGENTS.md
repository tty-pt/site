# Agent Development Guide

## Architecture

Current runtime:
- `axil` on port `8080` handles HTTP, auth, sessions, uploads, and business logic.
- `mods/ssr/` builds `ssr.so` from Rust with Dioxus SSR.
- `htdocs/song-client.js` loads the Rust/WASM song detail browser enhancements.
- `mods/choir/choir_fe.c` and `mods/songbook/songbook_fe.c` own the C/bud frontend handlers for choir and songbook detail/edit/add pages.
- `mods/song/song_fe.c` owns the C/bud frontend for song detail pages.
- `mods/common/site_ui_fe.c` owns the shared site layout, menu, and form helpers.
- `mods/source/source.c` (~708 lines) owns dataset CRUD, `/api/dataset/*` routes.
- Encoding helpers (`axil_url_encode`, `axil_url_decode`, `axil_json_escape`, `axil_slugify`) live in `external/axil/src/axil-encode.c` as direct C functions (not NDX hooks).

There is no Fresh/Deno proxy runtime in the request path anymore.

### Module dependency chain (load order enforced by `mods/core/core.c`)

```
axil (libaxil.so)
 ├── common.so    (~980 lines: JSON builders, response helpers, storage, str_trim)
 ├── ssr.so       (Rust SSR with Dioxus)
 ├── source.so    (dataset CRUD + /api/dataset/* routes)
 ├── mods.load     (poem, songbook, song, choir — loaded after explicit ndx_load calls)
 └── Others: auth, index, mpfd, redir
```

## Debug Logging System

All build output, runtime logs, and test results are automatically captured to `debug/` directory.

### Debug Directory Structure
```
debug/
├── builds/           # Compilation output (timestamped logs)
├── runtime/          # Server output (axil logs)
└── tests/            # E2E test results
```

### Debug Commands
```bash
make debug-logs                      # View recent logs (builds, tests, runtime)
make build-capture                   # Capture single build to log
make test-single-capture TEST=foo.ts  # Capture specific test
make test-capture                     # Capture all e2e tests
make debug-clean                      # Delete all debug logs
```

### Adding Debug Output in C
```c
fprintf(stderr, "DEBUG: my message %s\n", value);
```
Check output in `debug/runtime/axil.log` (server must be running via `make watch`).

See `debug/README.md` for full documentation.

## Build & Run

```bash
make
make clean
make distclean
make run
make watch    # Auto-rebuild and restart with logging
```

Notes:
- `make clean` removes built module artifacts but keeps Cargo caches.
- `make distclean` also runs deeper per-module cleanup, including `cargo clean`.
- If `8080` is busy: `PORT=8081 make run`
- `make watch` saves build logs to `debug/builds/` and runtime logs to `debug/runtime/axil.log`

WASM setup:

```bash
cargo install -f wasm-bindgen-cli
rustup target add wasm32-unknown-unknown
```

For manual startup:

```bash
AUTH_SKIP_CONFIRM=1 ./start.sh
```

`start.sh` automatically sets `LD_LIBRARY_PATH` to include `external/axil/lib/`
so the updated axil encoding symbols are found at runtime.

The `-m mods/core/core` flag is required. Without it no modules load, no
handlers are registered, and requests will crash or return unexpected errors.

## Testing

```bash
make unit-tests
make pages-test
make e2e-tests
make test
```

Run one e2e file at a time when needed:

```bash
deno test --allow-all tests/e2e/song-add.test.ts
```

e2e prerequisites:
- `axil` running on `:8080` with `AUTH_SKIP_CONFIRM=1` set
- `/tmp/site.log` writable (or check `debug/runtime/axil.log`)

**Important:** The `AUTH_SKIP_CONFIRM=1` environment variable must be set when
**starting the server**, not just when running tests. The auth module reads this
at startup to determine behavior.

## Chroot Setup

The site runs inside a chroot at the repo root. `ndx_install()` in `mods/auth/auth.c`
creates the `./etc/` files automatically on first start, but `sh` and its shared
libraries still need to exist inside the tree.

### Linux

```bash
mkdir -p ./bin ./lib ./lib/x86_64-linux-gnu
cp /bin/sh ./bin/sh
ldd /bin/sh
cp /lib/x86_64-linux-gnu/libnss_files.so.2 ./lib/x86_64-linux-gnu/
```

`./etc/passwd`, `./etc/shadow`, and `./etc/group` are maintained by the auth module.

### OpenBSD

Run once to bootstrap `pwd.db`/`spwd.db` for existing users:

```bash
pwd_mkdb -d /path/to/site/etc /path/to/site/etc/master.passwd
```

`pwd_mkdb` is called automatically on new registration afterwards.

## C Conventions

### Redirects

Every `303` redirect should use `axil_header_set` and `axil_respond`:

```c
axil_header_set(fd, "Location", location);
axil_respond(fd, 303, "");
return 0;
```

### Form parsing

- Use `axil_query_parse` + `axil_query_param` for `application/x-www-form-urlencoded`
- Use `call_mpfd_parse` + `call_mpfd_get` only for `multipart/form-data`
- GET handlers must call `axil_query_parse()` with `QUERY_STRING` env var before `axil_query_param()`. The `query_db` is emptied by `qmap_drop` at the end of every request (in `do_GET`/`do_POST`), so it starts empty on each request.

### Memory management

Never `free()` qmap-managed values.

```c
char *val = qmap_get(map, key);   /* do not free(val) */
```

`qmap_put(map, key, ptr)` takes ownership of `ptr`.

### Exported functions

Use `NDX_HOOK_DEF` in the host or `NDX_LISTENER` in modules, and `NDX_HOOK_DECL` in shared headers.

### Style

- **Tabs for indentation.** Not spaces. If you indent with spaces you are
  wrong, and I don't mean "wrong" in some aesthetic sense — I mean you are
  *objectively, factually wrong*. Tabs. One tab per level. This is enforced
  by `.clang-format` and there is nothing to discuss.
- **Maximum 4 levels of indentation.** If you need more than 4 levels, your
  code is broken. Not "suboptimal." Not "a bit hard to read." *Broken.* You
  have written garbage and you should feel bad. Extract a function. Invert a
  condition. Use a guard clause. Do literally anything except keep stacking
  indentation like it makes you look smart. It doesn't. This is enforced by
  `clang-tidy` — run `make lint` and fix every single warning before
  committing.
- System includes first, then local includes
- `snake_case` for functions/vars
- `UPPER_CASE` for macros
- C89 style: declare all local variables at the **top of their block**, before any
  statements. Never declare variables mid-block after a statement.
- One statement per line — no `;`-separated statements on the same line.
- Blank lines are permitted to separate logical sections within a function body,
  but use them sparingly.
- Run `make format` after editing C files to apply `clang-format` automatically.

## Rust SSR Layout

- `mods/<module>/ssr/src/main.rs` owns module-specific rendering.
- `mods/ssr/src/lib.rs` is the main SSR entry point (FFI shell, route dispatch).
- `mods/ssr/build.rs` auto-discovers module SSR files.
- `mods/ssr/Cargo.lock` is tracked.

## Common Pitfalls

1. Every C `303` redirect needs `Connection: close` + `DF_TO_CLOSE` before `axil_head`.
2. Do not use `call_mpfd_parse` for url-encoded forms.
3. Do not `free()` qmap-managed memory.
4. `axil_register_handler` is last-registration-wins.
5. Do not commit build artifacts like `*.so`, `*.o`, swap files, or Rust `target/`.
6. Required data dirs: `items/poem/items`, `items/song/items`, `items/songbook/items`, `items/choir/items`.
7. `QM_REFERENCE` field values are stored as binary `uint32_t` positions, not strings. To read a reference: `const uint32_t *ref = (const uint32_t *)qmap_get(hd, "id:field");` then resolve via `qmap_get_key(target_hd, *ref)`. Do NOT treat the return of `qmap_get` for a reference field as a `const char *`.

## Troubleshooting

- Module not loading: check `mods.load` and verify the `.so` exists.
- Redirect hangs: missing `Connection: close` / `DF_TO_CLOSE`.
- C handler not called: inspect `/tmp/site.log` or `debug/runtime/axil.log`.
- Rust SSR build issues on OpenBSD: rebuild `mods/ssr` and inspect `mods/ssr/Makefile`.
- C frontend changes need module rebuild + server restart: `touch mods/<mod>/<mod>.c && make -C mods/<mod>` then restart axil.

## e2e

The Playwright e2e suite still uses Deno as the runner. That is the main remaining
Deno dependency in this repo.
