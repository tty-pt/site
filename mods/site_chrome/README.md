# site_chrome — Global Browser Chrome Enhancement (WASM-only)

Browser-side WebAssembly enhancement for global site navigation, mobile menu drawer toggling, theme switching, and client-side interactions.

## Overview

`mods/site_chrome` is a **WASM-only module** (`WASM_ONLY=1` in its Makefile). It has no native `.so` companion on the server. Instead, it compiles `mods/site_chrome/ux/site_chrome.c` to `htdocs/site_chrome.wasm` for progressive enhancement in the browser.

## Features

- **Mobile Navigation Drawer:** Handles click and touch interactions for toggling site navigation menus (`[data-menu-toggle]`).
- **Esc Key Handling:** Closes open menus, modals, and drawers when the Escape key is pressed.
- **Click-Outside Detection:** Automatically closes active dropdowns and drawers when clicking outside their container boundaries.

## Architecture

- **Entry point:** Compiled to `wasm32-wasi` reactor format.
- **Hydration:** Loaded by `htdocs/bud-client.js` on pages with `data-modules="site_chrome"` or combined with module WASMs.
- **Purity:** Strict C-isomorphic WASM purity (depends only on `bud.h`, `bud_app.h`, and pure C standard library).

## Related Docs

- `docs/WASM-BRIDGE.md` — Client-side WASM loader and event hydration.
- `docs/C-ISOMORPHIC-BUD.md` — C-isomorphic compilation rules.
