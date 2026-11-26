# apps/nd-native — Native Desktop NeverDark Client

Standalone pure-C desktop client compiling the unmodified NeverDark isomorphic UI (`mods/nd/ux/*.cx`, `mods/nd/ux/*.c`) and Seven 2.5D/3D engine (`mods/nd/seven/`) directly against native OpenGL/GLFW (`liblamina`), `lib/bud-lamina`, and native WebSockets (`AXIL_WS_NATIVE`).

## Features

- **Zero-Divergence UI & 3D Engine**: Compiles the exact same `.cx` / C UI trees, CSS stylesheets (`styles.css`, `hyle.css`, `nd.css`), and Seven WebGL2 engine used in `htdocs/nd.wasm`.
- **Native OpenGL 3.3 Core & GLFW**: Runs at 60 Hz with DPI-aware scaling, TrueType/emoji font rendering (`lam_font_open_ttf`), and isolated WebGL2 stage surfaces (`bud_lamina_webgl_begin`/`end`).
- **Native WebSocket + TLS**: Connects directly to `axil-nd` via `lib/bud-lamina/axil_ws_native.c` (`ws://` / `wss://`), decoding `#b` Binary Client Protocol frames and ANSI terminal streams.

## Build

```sh
make -C lib/bud-lamina
make -C apps/nd-native
```

## Usage

```sh
./apps/nd-native/nd-native [options] [ws-url]
```

| Flag | Env Var | Default | Description |
|------|---------|---------|-------------|
| `--url <url>` | `ND_URL` | `ws://127.0.0.1:8080/nd` | NeverDark WebSocket endpoint |
| `--cookie <str>` | `ND_COOKIE` | *(none)* | Session cookie header (`QSESSION=...`) |
| `--htdocs <dir>` | `ND_HTDOCS` | `htdocs` | Asset root for `styles.css`, `hyle.css`, `nd.css`, and images |
| `--driver <name>` | `ND_DRIVER` | `lamina-seven` | Initial viewport driver (`lamina-seven`, `lamina-2d`, `tty`, `bud`) |
| `--size <WxH>` | — | `1280x800` | Initial window size (minimum `320x240`) |
| `--send <cmd>` | — | *(none)* | Command sent automatically on WebSocket open |
| `--frames <N>` | — | `0` (unlimited) | Exit cleanly after rendering `N` frames |
| `--screenshot <ppm>` | — | *(none)* | Dump final framebuffer to PPM when `--frames` completes |
| *(env only)* | `ND_SCALE` | auto DPI | Override DPI scale (`1.0`–`4.0`) |
