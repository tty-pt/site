# NDC + Rust SSR Site

> For development conventions, see [AGENTS.md](./AGENTS.md).

This site runs as an NDC application with Rust SSR.

## Architecture

- `ndc` handles HTTP, auth, sessions, file uploads, and business logic.
- `mods/ssr/ssr.c` bridges NDC to the Rust renderer.
- `mods/ssr/rust-renderer/` renders HTML with Dioxus SSR.
- `htdocs/wasm.js` loads the browser-side wasm modules.
- `mods/song/client/` and `mods/songbook/client/` provide Rust/WASM browser enhancements, built with `wasm-bindgen`.

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

- `make` builds all modules and rebuilds the checked-in wasm browser assets when the local wasm toolchain is available
- `make clean` removes built module artifacts but keeps Cargo caches
- `make distclean` also runs deeper per-module cleanup, including Rust `cargo clean`

## WASM Setup

If you want `make` to rebuild the song client wasm locally, install the CLI and target once:

```bash
cargo install -f wasm-bindgen-cli
rustup target add wasm32-unknown-unknown
```

Then verify:

```bash
wasm-bindgen --version
rustup target list --installed | grep wasm32-unknown-unknown
```

On OpenBSD, `cargo install` is the expected path for `wasm-bindgen-cli`.

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
- `wasm-bindgen` CLI + `rustup target add wasm32-unknown-unknown` if you want to rebuild the wasm browser assets locally
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
- The browser enhancement path is now wasm-driven; there is no handwritten `app.js` runtime left.
- The Rust lockfile is tracked at [mods/ssr/rust-renderer/Cargo.lock](/home/quirinpa/site/mods/ssr/rust-renderer/Cargo.lock).
- The old Fresh/proxy frontend tree has been removed.
