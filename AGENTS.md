# Agent Development Guide

## Architecture

Current runtime:
- `ndc` on port `8080` handles HTTP, auth, sessions, uploads, and business logic.
- `mods/ssr/ssr.c` calls the Rust renderer directly.
- `mods/ssr/rust-renderer/` renders HTML with Dioxus SSR.
- `htdocs/wasm.js` loads the Rust/WASM browser enhancements.
- `mods/song/client/` and `mods/songbook/client/` own the current browser-side enhancement code.

There is no Fresh/Deno proxy runtime in the request path anymore.

## Build & Run

```bash
make
make clean
make distclean
make run
```

Notes:
- `make clean` removes built module artifacts but keeps Cargo caches.
- `make distclean` also runs deeper per-module cleanup, including `cargo clean`.
- If `8080` is busy: `PORT=8081 make run`
- `make` only rebuilds the wasm browser assets when both `wasm-bindgen` and the `wasm32-unknown-unknown` target are installed.

WASM setup:

```bash
cargo install -f wasm-bindgen-cli
rustup target add wasm32-unknown-unknown
```

For manual startup:

```bash
cd /home/quirinpa/site
setsid ndc -C . -p 8080 -d >> /tmp/site.log 2>&1 &
```

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
- `ndc` running on `:8080`
- `/tmp/site.log` writable

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

Every `303` redirect should use `ndc_header_set` and `ndc_respond`:

```c
ndc_header_set(fd, "Location", location);
ndc_respond(fd, 303, "");
return 0;
```

### Form parsing

- Use `call_query_parse` + `call_query_param` for `application/x-www-form-urlencoded`
- Use `call_mpfd_parse` + `call_mpfd_get` only for `multipart/form-data`

### Memory management

Never `free()` qmap-managed values.

```c
char *val = qmap_get(map, key);   /* do not free(val) */
```

`qmap_put(map, key, ptr)` takes ownership of `ptr`.

### Exported functions

Use `NDX_DEF` in the implementation and `NDX_DECL` in the header.

### Style

- Tabs for indentation
- System includes first, then local includes
- `snake_case` for functions/vars
- `UPPER_CASE` for macros

## Rust SSR Layout

- `mods/<module>/ssr.rs` owns module-specific rendering.
- `mods/ssr/rust-renderer/src/lib.rs` is the generic FFI shell.
- `mods/ssr/rust-renderer/build.rs` auto-discovers module SSR files.
- `mods/ssr/rust-renderer/Cargo.lock` is tracked.

## Common Pitfalls

1. Every C `303` redirect needs `Connection: close` + `DF_TO_CLOSE` before `ndc_head`.
2. Do not use `call_mpfd_parse` for url-encoded forms.
3. Do not `free()` qmap-managed memory.
4. `ndc_register_handler` is last-registration-wins.
5. Do not commit build artifacts like `*.so`, `*.o`, swap files, or Rust `target/`.
6. Required data dirs: `items/poem/items`, `items/song/items`, `items/songbook/items`, `items/choir/items`.

## Troubleshooting

- Module not loading: check `mods.load` and verify the `.so` exists.
- Redirect hangs: missing `Connection: close` / `DF_TO_CLOSE`.
- C handler not called: inspect `/tmp/site.log`.
- Rust SSR build issues on OpenBSD: rebuild `mods/ssr` and inspect `mods/ssr/Makefile`.

## e2e

The Playwright e2e suite still uses Deno as the runner. That is the main remaining
Deno dependency in this repo.
