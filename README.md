# Site

Pure-C music site (poem, song, gig, grp) on top of the axil HTTP library,
the libxylem XY module system, the hyle data engine, and the bud HTML builder.

> For development conventions, see [AGENTS.md](./AGENTS.md).

## Architecture

- `axil` handles HTTP, auth, sessions, file uploads, and business logic.
- All request handlers are C functions compiled into `mods/*/*.so`, loaded via
  the libxylem XY module system (`RTLD_LOCAL` — cross-module calls go through
  XY dispatch only).
- `hyle` provides the pure schema, query, and full-text search (FTS) engine.
- `libhyle-source` (`external/hyle/c/libhyle-source`) provides dataset persistence,
  metadata I/O, and pluggable storage drivers (`store_fs`, `store_mem`, custom stores).
- `libhyle-bud` (`external/hyle/c/libhyle-bud`) bridges Hyle data components to Bud DOM rendering.
- HTML is built server-side with **bud** (a C DOM/SSR scaffold). There is no
  Rust SSR / Dioxus / Fresh / Deno proxy in the request path.
- The only WASM is C compiled to `wasm32-wasi` from the same sources as the
  `.so` modules (e.g. `mods/song/ux/detail.c` → `htdocs/song_detail.wasm`),
  loaded at runtime by `htdocs/bud-client.js` for browser-side enhancement.
- Pages are SSR-first and work without JS; interactivity is progressive
  enhancement (see `docs/DESIGN.md`).

## Quick Start

```bash
mkdir -p var/poem var/song var/gig var/grp var/song.types

make
make watch
```

Then open `http://localhost:8080`.

`make watch` automatically captures build and runtime logs to `debug/` directory.

If port `8080` is already in use:

```bash
PORT=8081 make run
```

The server runs inside a chroot rooted at the repo root. See `docs/BUILD.md`
"Running the server" for the `sh` + shared-libs prerequisites.

## Build

```bash
make          # build everything
make clean    # remove built module artifacts
make distclean
```

- `make` builds all modules. WASM browser assets are built by the `build.mk`
  WASI rule only when a WASI toolchain is available (probe-skipped otherwise).
- To rebuild the `.wasm` assets locally, install the [WASI SDK](https://github.com/WebAssembly/wasi-sdk)
  and point `WASI_CC` at it, or `apt install wasi-libc` on Debian/Ubuntu.

## Test

```bash
make unit-tests   # module test.sh suites — needs axil running on :8080
make pages-test   # page-render smoke tests (curl :8080 for DOCTYPE)
make e2e-tests    # Playwright via Deno — needs server + AUTH_SKIP_CONFIRM=1
make test         # all of the above
```

Run a single e2e file:

```bash
deno test --allow-all tests/e2e/song-add.test.ts
```

**Note:** e2e tests require the server running with `AUTH_SKIP_CONFIRM=1`
set at startup (not just at test time). `make watch` sets it automatically.

## Debug Logging

The `debug/` directory captures build output, runtime logs, and test results:

```bash
make debug-logs      # View recent logs
make build-capture   # Rebuild + save log
make test-capture    # Run e2e + save
make debug-clean     # Clear debug logs
```

## Requirements

- C compiler (`clang` for WASM builds)
- `axil`, `libxylem`, `libqmap`, `stoma`, `hyle`, `bud` (submodules under `external/`)
- Deno, only for the Playwright e2e test runner

## Modules

- `/auth` — registration, login, sessions, and CSRF protection
- `/common` — shared response, storage, and declarative UI builders (`site_ui_form_from_desc`, `site_ui_picker`)
- `/source` — dataset registration and HTTP API transport over `libhyle-source`
- `/index` — generic CRUD routes, list views, and picker option endpoints
- `/poem` — poem upload and listing
- `/song` — song detail, chord charts, transposition, and media attachments
- `/grp` — group repertoire management and format categories
- `/gig` — gigs / setlists with ordered songs, transpose settings, and formats
- `/mpfd` — multipart/form-data parser
- `/site_chrome` — WASM client chrome enhancement (`htdocs/site_chrome.wasm`)
