# mods/nd — Pure-C Isomorphic Bud UI for NeverDark

Pure-C isomorphic user interface for NeverDark (`axil-nd`), dual-compiled:
1. Native SSR renderer (`nd_ui_render`) called by `external/axil-nd/src/http.c` inside `site_ui_page_lang`.
2. Browser WASM reactor (`htdocs/nd.wasm`) loaded via `bud-client.js` on pages with `data-modules="site_chrome nd"`.

## Architecture

- **No Web Components**: All HUD widgets (gauges, direction keypad, action docks, modal overlays) are pure-C Bud node trees (`bud_tpl`, `bud_element`, `bud_bind`).
- **No JS State Machine**: State is held in C struct `nd_client_state_t` (`include/nd/bcp.h`), decoded directly from `#b` Binary Client Protocol packets via `nd_bcp_decode()`.
- **No Popper**: Modals for character sheet (stats) and equipment doll are standard CSS overlays toggled via Bud patch operations and `bud_app_set_state()`.
- **Generic Terminal Bridge**: Renders `<div id="term" data-bud-terminal="1">`. `htdocs/bud-hydrate.js` discovers the attribute, lazily mounts xterm.js, and bridges I/O to WASM exports/imports (`wasm_term_on_input`, `bud_host_term_write`).
