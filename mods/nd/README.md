# mods/nd — Pure-C Isomorphic Bud UI & Seven 3D Engine for NeverDark

Pure-C / `.cx` isomorphic user interface (`mods/nd/ux/`) and Seven 2.5D/3D WebGL2 engine (`mods/nd/seven/`) for NeverDark (`axil-nd`), compiled across three targets:
1. Native SSR renderer (`nd.so` / `nd_ui_render`) called by `external/axil-nd/src/http.c` inside `site_ui_page_lang`.
2. Browser WASM reactor (`htdocs/nd.wasm`, built with `-O3 -msimd128`) loaded via `htdocs/stele-client.js` on pages with `data-modules="site_chrome nd"`.
3. Standalone native desktop client (`apps/nd-native/nd-native`) via `lib/bud-lamina` (`liblamina` OpenGL/GLFW WebGL2 + native CSS/UI + `axil_ws_native`).

## Architecture

- **Pure-C & `.cx` Reactive UI (`mods/nd/ux/`)**: All HUD widgets (gauges, direction keypad, action docks, Settings modal, character/equipment overlays) are pure-C `.cx` / Bud node trees (`nd_ui.c`, `nd_win.c`, `nd_hud_ui.cx`, `nd_shell_ui.cx`, `nd_settings_ui.cx`, `nd_map_ui.cx`, `nd_dock_ui.cx`, `nd_state_io.c`, `ux/glue/{attr_ui.cx,equip_ui.cx,spell_ui.c,fight_ui.c,mortal_ui.c}`).
- **Seven 2.5D/3D Engine (`mods/nd/seven/`)**: Extracted toroidal planetary terrain, volumetric clouds, water optics, 3D botanical/mineral mesh engine, and WebGL2 renderer (`nd_seven.c`, `nd_seven_atlas*.c`, `nd_seven_mesh3d.c`, `nd_seven_ground.c`, `nd_seven_motion.c`, `nd_seven_objects.c`, `nd_seven_gl*.c`, with build-time GLSL minification via `scripts/minify-glsl-c.py` → `nd_seven_gl_shaders.gen.c`).
- **No JS State Machine**: State is held in C struct `nd_client_state_t` (`external/axil-nd/include/nd/bcp.h`), decoded directly from `#b` Binary Client Protocol packets via `nd_bcp_decode()`.
- **Pure-C Binary Map Pipeline**: Room maps are transmitted over BCP opcode 1 (`ND_BCP_VIEW`) as packed `nd_map_data_t` structs (7x7 grid of `nd_map_tile_t` cells). The client renders symmetrically via ASCII Bud DOM, `liblamina` mini-tilemap canvas, or the Seven 2.5D/3D WebGL2 stage.
- **Zero-Loss SSR Hydration**: In `nd_state_serialize()`, binary map data is encoded into a hex string (`map_hex`) embedded in `<script id="bud-state">`, eliminating escaping and mojibake issues. In `wasm_init()`, `map_hex` decodes directly back to `client_state.map_data` byte-for-byte.
- **Hierarchical Regional Governance**: Stage drivers (`tty`, `lamina-2d`, `lamina-seven`, `bud`), map drivers (`ascii-bud`, `canvas-lamina`, `none`), and layout modes (`hybrid`, `expanded`, `fullscreen`) are governed by Corm DB spacetime octree rules (`nd-region`) and delivered via opcode 10 (`ND_BCP_REGION`).
- **No Popper**: Modals for character sheet (stats), equipment doll, and graphics settings (resolution scale, quality presets, MSAA, DRS) are pure CSS/Bud overlays toggled via `bud_app_set_state()`.
- **Modular Capability Bridges**: Renders `<div id="term" data-bud-terminal="1">` and `<canvas>` surfaces wired via `htdocs/stele-client.js`, `htdocs/bud-cap.js`, `htdocs/axil-tty-cap.js`, `htdocs/axil-cap.js`, `htdocs/lamina-cap.js`, and `htdocs/webgl-cap.js` (and natively via `lib/bud-lamina` in `apps/nd-native`).
