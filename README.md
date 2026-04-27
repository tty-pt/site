# NDC + Rust SSR Site

> For development conventions, see [AGENTS.md](./AGENTS.md).

This site runs as an NDC application with Rust SSR.

## Architecture

- `ndc` handles HTTP, auth, sessions, file uploads, and business logic.
- `mods/ssr/ssr.c` bridges NDC to the Rust renderer.
- `mods/ssr/rust-renderer/` renders HTML with Dioxus SSR.
- `htdocs/app.js` provides the small amount of client-side enhancement.

There is no Fresh/Deno proxy runtime in the request path anymore.

## Quick Start

```bash
mkdir -p items/poem/items items/song/items items/songbook/items items/choir/items

make
./start.sh
```

Then open `http://localhost:8080`.

If port `8080` is already in use:

```bash
PORT=8081 make run
```

## Build

```bash
make
make clean
make distclean
```

- `make` builds all modules
- `make clean` removes built module artifacts but keeps Cargo caches
- `make distclean` also runs deeper per-module cleanup, including Rust `cargo clean`

## Test

```bash
make unit-tests
make pages-test
make e2e-tests
make test
```

You can also run one e2e file at a time:

```bash
deno test --allow-all tests/e2e/song-add.test.ts
```

## Requirements

- C compiler
- Rust/Cargo
- `ndc`, `ndx`, `qmap`
- Deno only for the Playwright e2e test runner

## Modules

- `/auth`
- `/poem`
- `/song`
- `/choir`
- `/songbook`

## Notes

- Checked-in browser assets live in [htdocs](/home/quirinpa/site/htdocs).
- The Rust lockfile is tracked at [mods/ssr/rust-renderer/Cargo.lock](/home/quirinpa/site/mods/ssr/rust-renderer/Cargo.lock).
- The old Fresh/proxy frontend tree has been removed.
