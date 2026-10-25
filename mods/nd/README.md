# mods/nd — Pure-C Isomorphic Bud UI for NeverDark

Pure-C isomorphic user interface for NeverDark (`axil-nd`), dual-compiled:
1. Native SSR renderer (`nd_ui_render`) called by `external/axil-nd/src/http.c` inside `site_ui_page_lang`.
2. Browser WASM reactor (`htdocs/nd.wasm`) loaded via `bud-client.js` on pages with `data-modules="site_chrome nd"`.

## Architecture

- **No Web Components**: All HUD widgets (gauges, direction keypad, action docks, modal overlays) are pure-C Bud node trees (`bud_tpl`, `bud_element`, `bud_bind`).
- **No JS State Machine**: State is held in C struct `nd_client_state_t` (`include/nd/bcp.h`), decoded directly from `#b` Binary Client Protocol packets via `nd_bcp_decode()`.
- **Pure-C Binary Map Pipeline**: Room maps are transmitted over BCP opcode 1 (`ND_BCP_VIEW`) as packed 400-byte `nd_map_data_t` structs (7x7 grid of 8-byte `nd_map_tile_t` cells). The client renders symmetrically via `render_room_map_ascii` (clean Bud DOM text) or `render_room_map_canvas` (`liblamina` mini-tilemap canvas).
- **Zero-Loss SSR Hydration**: In `nd_state_serialize()`, binary map data is encoded into an 800-character hex string (`map_hex`) embedded in `<script id="bud-state">`, completely eliminating escaping and mojibake issues. In `wasm_init()`, `map_hex` decodes directly back to `client_state.map_data` byte-for-byte.
- **Hierarchical Regional Governance**: Stage drivers (`tty`, `lamina-2d`, `bud`), map drivers (`ascii-bud`, `canvas-lamina`, `none`), and layout modes (`hybrid`, `expanded`, `fullscreen`) are governed by Corm DB spacetime octree rules (`nd-region`) and delivered via opcode 10 (`ND_BCP_REGION`).
- **No Popper**: Modals for character sheet (stats) and equipment doll are standard CSS overlays toggled via Bud patch operations and `bud_app_set_state()`.
- **Generic Terminal Bridge**: Renders `<div id="term" data-bud-terminal="1">`. `htdocs/bud-hydrate.js` discovers the attribute, lazily mounts xterm.js, and bridges I/O to WASM exports/imports (`wasm_term_on_input`, `bud_host_term_write`).
