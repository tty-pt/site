# NeverDark Client Architecture — Pluggable Viewports & liblamina

This document specifies the pluggable viewport architecture for NeverDark (`axil-nd`), enabling modders to deploy custom rendering surfaces (such as 2D graphical tilemaps rendered via `liblamina`) while preserving the pure-C isomorphic Bud UI shell (SSR + WASM) and strict separation of concerns across house libraries.

---

## 1. Architectural Separation of Concerns

Our tech stack strictly partitions responsibilities across dedicated libraries:

| Subsystem | Owner Library | Domain Responsibilities |
|---|---|---|
| **UI Components & DOM** | `libbud` | Virtual DOM, SSR HTML hydration, VDOM diffing, patch ops (`bud_patch_*`). Zero networking or canvas code. |
| **Stream Transport & Terminal** | `axil-tty` / `<ttypt/ws.h>` | WebSocket client streaming (`ws_connect`, `ws_send`, `ws_close`) and terminal emulation (`term_write`). |
| **2D Graphics & Canvas** | `liblamina` | Immediate-mode 2D graphics, sprite sheets, tilemaps (`lam-tm.h`), and canvas frame presentation (`lam_canvas_flush`). |
| **MUCK Engine & Simulation** | `axil-nd` | World persistence, room topology, entity simulation, and Binary Client Protocol (`#b` BCP) packet encoding. |

By maintaining this separation:
- `htdocs/bud-hydrate.js` remains 100% pure upstream `libbud` DOM code.
- `scripts/wasm-allowed-imports.lst` stays strictly limited to the 6 canonical UI host imports (`bud_host_*`).
- Capability bridges (`htdocs/bud-capabilities.js`) and library allowlists (`external/*/wasm-allowed-imports.lst`) are composable and decentralized.

---

## 2. Pluggable Viewport Layout

In the NeverDark client (`mods/nd/ux/`), the user interface is cleanly divided into the Bud UI shell and the swappable Viewport slot:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Bud UI Shell (mods/nd/ux)                       │
│  [Top Bar / Auth]                                       [Close Modal]  │
├─────────────────────────────────────────┬──────────────────────────────┤
│                                         │       Control Deck (Bud)     │
│             Viewport Slot               │  ┌─────────────────────────┐ │
│                                         │  │   Directional Keypad    │ │
│   Mode A: <div data-bud-terminal>       │  │      [k] [K] [l] ...    │ │
│           (xterm.js ANSI Stream)        │  ├─────────────────────────┤ │
│                                         │  │   HP / MP Status Bars   │ │
│   Mode B: <canvas data-bud-canvas>      │  │      [======= 100%]     │ │
│           (liblamina 2D Tilemap)        │  ├─────────────────────────┤ │
│                                         │  │   Dynamic Action Dock   │ │
│                                         │  │   [📋] [👕] [📦] [👁]   │ │
│                                         │  │   [🎮 2D / 📺 TTY]      │ │
│                                         │  └─────────────────────────┘ │
├─────────────────────────────────────────┴──────────────────────────────┤
│  Modals (Pure CSS Overlays): Character Sheet (Stats), Equipment Doll   │
└────────────────────────────────────────────────────────────────────────┘
```

The user toggles viewports at runtime by clicking `[🎮 2D / 📺 TTY]` in `#g-actions`.

---

## 3. Viewport Modes

### Mode A: TTY Terminal Viewport (Default)
- **Container Element**: `<div id="term" data-bud-terminal="1">`
- **Render Engine**: `xterm.js` lazily mounted via `createTerminalCapability()`.
- **Data Flow**:
  - Outbound: Keystrokes -> `wasm_term_on_input()` -> `ws_send()` to server.
  - Inbound: Server emits ANSI text -> `wasm_ws_on_message()` -> `term_write()` to xterm.
- **Use Case**: Traditional MUCK/MUD gameplay, command-line inspection (`help`, `man`), and wizard building (`sh`, `loadmod`).

### Mode B: Graphical 2D Viewport via liblamina
- **Container Element**: `<canvas id="viewport" data-bud-canvas="1" width="640" height="480">`
- **Render Engine**: `liblamina` (`mods/nd/ux/nd_lamina.c` / `nd_lamina.h`).
- **Data Flow**:
  - Inbound: Server emits BCP packets (`#b`).
  - C Parser: `nd_bcp_decode()` updates `nd_client_state_t`.
  - Render Loop: `wasm_canvas_render()` calls `nd_lamina_render_room()`:
    - Draws 16x12 room tile grid (walls, floor tiles, doorways).
    - Draws player avatar in center room coordinates.
    - Draws target mob/entity if selected.
    - Draws real-time health indicator bar above avatar.
    - Flushes pixel words to canvas via `lam_canvas_flush()`.
- **Use Case**: 2D top-down RPG view, tactical combat visualization, and modded graphics.

---

## 4. Decentralized Linker Allowlist Architecture

Rather than maintaining a central allowlist of every symbol needed by every module:
1. `scripts/wasm-allowed-imports.lst` contains only canonical `libbud` UI symbols:
   ```
   env.bud_host_log
   env.bud_host_mark_dirty
   env.bud_host_flush
   env.bud_host_fetch
   env.bud_host_set_location
   env.bud_host_emit_patch
   ```
2. Libraries package their own capability allowlists:
   - `external/axil-tty/wasm-allowed-imports.lst`: `env.ws_connect`, `env.ws_send`, `env.ws_close`, `env.term_write`
   - `external/liblamina/wasm-allowed-imports.lst`: `env.lam_canvas_flush`
3. `build.mk` composes allowlists dynamically:
   ```makefile
   extra_allowed=""; \
   for f in $($*-wasm-allowed) $(EXTRA_WASM_ALLOWED); do \
       if [ -f "$$f" ]; then extra_allowed="$$extra_allowed -Wl,--allow-undefined-file=$$f"; fi; \
   done; \
   $(WASI_CC) ... $(WASM_LDFLAGS) $$extra_allowed -o $@ ...
   ```
   If a library list is absent or a submodule is not checked out, `build.mk` cleanly skips it without build errors.

---

## 5. Composable Browser Runtime (`bud-client.js`)

`htdocs/bud-client.js` acts as an open, generic capability orchestrator. It contains zero domain-specific code and dynamically loads declared capability bridges via `data-capabilities`:

```javascript
import { createBudWasmBridge } from './bud-hydrate.js?v=1';

// Open capability registry
window.__wasm_capability_registry = window.__wasm_capability_registry || {};
window.registerWasmCapability = (namespace, factory) => {
    window.__wasm_capability_registry[namespace] = factory;
};

// Dynamic capability loading based on page declaration
const declaredCaps = (document.body.getAttribute('data-capabilities') || '').trim().split(/\s+/).filter(Boolean);
for (const cap of declaredCaps) {
    if (!window.__wasm_capability_registry[cap]) {
        try {
            await import(`/${cap}-bridge.js`);
        } catch (_) {}
    }
}

// Two-level library-namespaced WASM imports
const budImports = bridge.makeImports();
const imports = {
    bud: budImports,
    env: { ...budImports, __main_argc_argv: () => 0 },
    wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 })
};

for (const [ns, factory] of Object.entries(window.__wasm_capability_registry)) {
    const capBridge = factory(spec.root, getWasm);
    if (capBridge && typeof capBridge.makeImports === 'function') {
        imports[ns] = capBridge.makeImports();
    }
}

WebAssembly.instantiate(wasmBytes, imports);
```

Third-party modders can register custom runtime capabilities without modifying core files by serving an ES module that calls `window.registerWasmCapability(namespace, factory)`.
