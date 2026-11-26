# lib/bud-lamina — Native Bud-to-Lamina Desktop Runtime

Static C bridge library (`libbud-lamina.a`) connecting `libbud` virtual DOM trees and canonical web CSS stylesheets to `liblamina`'s native OpenGL/GLFW UI and WebGL2 renderer.

## Components

- **`bud_lamina.h` / `bud_lamina.c`** — Reconciles `bud_node` trees (including `<body>` roots) onto `lam_ui_div_t` trees, applies loaded CSS rules (`bud_lamina_load_css_file`, `bud_lamina_set_css_var`), rasterizes inline SVGs, styles native controls, dispatches pointer/keyboard events with isolated WebGL2 state, and bridges `<canvas>` surfaces (`bud_lamina_webgl_begin`/`end`).
- **`axil_ws_native.h` / `axil_ws_native.c`** — Native RFC 6455 WebSocket + optional OpenSSL TLS (`AXIL_WS_TLS`) transport implementing the `AXIL_WS_NATIVE` declarations in `<ttypt/axil-ws.h>`.
- **`term_ansi.h` / `term_ansi.c`** — Embedded ANSI escape-sequence terminal emulator backing `#term` surfaces (`bud_lamina_term_write`).
- **`bl_img_decode.h` / `bl_img_decode.c` / `stb_image.h`** — Local image decoding (`bud_lamina_provide_img`, `bud_lamina_set_asset_root`).

## Build & Test

```sh
make -C lib/bud-lamina
make -C lib/bud-lamina test        # runs test_ws_native and test_bud_lamina
make -C lib/bud-lamina test-asan   # runs test_bud_lamina under ASan/UBSan/LSan
```
