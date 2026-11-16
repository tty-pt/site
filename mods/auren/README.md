# auren — Browser LLM Inference Sandbox

Interactive in-browser LLM inference demo powered by WebGPU / WebAssembly and pure-C isomorphic UI rendering.

## Overview

The `auren` module serves the `/auren` browser inference sandbox page. It renders an initial SSR view via `libbud` (`auren_ui_render_view`) and progressively hydrates in the browser via `htdocs/auren.wasm` (`auren_fe.c` + `auren_ui.c`) and `/auren-client.js` to run local quantized instruction models (defaulting to `smollm2-135m-instruct`) directly in the browser with WebGPU acceleration and model cache state tracking.

## SSR Routes

| Method | Route | Description |
|--------|-------|-------------|
| `GET` | `/auren` | Render the Auren Browser LLM interactive interface |
| `GET` | `/auren/` | `301 Moved Permanently` redirect to `/auren` |

## Architecture & Files

- `auren.c` — Axil HTTP route handler (`/auren`, `/auren/`) and `xy_install()` entrypoint.
- `auren-types.h` — Shared state structure (`auren_demo_state_t`: model selection, status, WebGPU availability, cache progress, streaming state, and message history).
- `ux/auren_ui.c` / `ux/auren_ui.h` — Pure-C isomorphic `libbud` component tree compiled into both `auren.so` (native SSR) and `htdocs/auren.wasm` (client hydration).
- `ux/auren_fe.c` — Browser WASM frontend controller (`STL_WASM_MODULE`) managing prompt submission, streaming token updates, and status transitions.

## Dependencies

- `mods/common/common` — Shared page shell (`site_ui_respond_page`), layout, and chrome integration.
- `libbud` — Isomorphic SSR + WASM VDOM rendering.

## Building & Testing

```sh
make -C mods/auren
make test-mod MOD=auren
```
