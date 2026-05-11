# AXIL + Rust SSR Site

> For development conventions, see [AGENTS.md](./AGENTS.md).

This site runs as an AXIL application with Rust SSR.

## Architecture

- `axil` handles HTTP, auth, sessions, file uploads, and business logic.
- `mods/ssr/` builds `ssr.so` from Rust with Dioxus SSR.
- `htdocs/song-client.js` loads the Rust/WASM song detail browser enhancements.
- `mods/song/client/` owns the current browser-side enhancement code, built with `wasm-bindgen`.

There is no Fresh/Deno proxy runtime in the request path anymore.

## Quick Start

```bash
mkdir -p items/poem/items items/song/items items/songbook/items items/choir/items

make
make watch
```

Then open `http://localhost:8080`.

`make watch` automatically captures build and runtime logs to `debug/` directory.

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

### Bud demo (C/WASM)

For `make` to rebuild `htdocs/bud_demo.wasm` locally:

```bash
# Debian/Ubuntu
sudo apt install wasi-libc lld-18 libclang-rt-18-dev-wasm32
```

Or download the [WASI SDK](https://github.com/WebAssembly/wasi-sdk) and override
`WASI_CC` in `external/bud/Makefile`.

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

For test debugging, see [debug/README.md](debug/README.md):
- `make debug-logs` - View recent logs
- `make test-single-capture TEST=foo.test.ts` - Capture specific test
- `make build-capture` - Capture build output

**Note:** e2e tests require the server running with `AUTH_SKIP_CONFIRM=1`. When using `make watch`, this is set automatically.

## Debug Logging

The `debug/` directory captures build output, runtime logs, and test results:

```bash
make debug-logs      # View recent logs
make debug-clean     # Clear debug logs
```

See [debug/README.md](debug/README.md) for full documentation.

## Requirements

- C compiler
- Rust/Cargo
- `wasm-bindgen` CLI + `rustup target add wasm32-unknown-unknown` if you want to rebuild the wasm browser assets locally
- `axil`, `ndx`, `qmap`
- Deno only for the Playwright e2e test runner

## Modules

- `/auth`
- `/poem`
- `/song`
- `/choir`
- `/songbook`

## Notes

- Checked-in browser assets live in [htdocs](htdocs).
- The browser enhancement path is now wasm-driven; there is no handwritten `app.js` runtime left.
- The Rust lockfile is tracked at [mods/ssr/Cargo.lock](mods/ssr/Cargo.lock).
- The old Fresh/proxy frontend tree has been removed.
