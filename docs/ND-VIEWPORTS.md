# NeverDark Client Architecture — Pluggable Viewports & liblamina

This document specifies the pluggable viewport architecture for NeverDark (`axil-nd`), enabling modders to deploy custom rendering surfaces (such as 2D graphical tilemaps rendered via `liblamina`) while preserving the pure-C isomorphic Bud UI shell (SSR + WASM) and strict separation of concerns across house libraries.

---

## 1. Architectural Separation of Concerns & UI Decoupling

NeverDark enforces strict separation of concerns across dedicated systems libraries, decoupling backend simulation completely from frontend presentation:

| Subsystem | Owner Library | Domain Responsibilities |
|---|---|---|
| **UI Components & DOM Shell** | `libbud` | Pure-C Virtual DOM, SSR HTML hydration, VDOM diffing, patch ops (`bud_patch_*`), responsive RPG HUD. Zero networking or canvas code. |
| **Stream Transport & Terminal** | `axil-tty` / `<ttypt/ws.h>` | WebSocket client streaming (`ws_connect`, `ws_send`, `ws_close`) and ANSI terminal emulation (`term_write`). |
| **Visual Graphics & Tilemaps** | `liblamina` | Immediate-mode 2D graphics, sprite sheets, tilemaps (`lam-tm.h`), fonts, and flexbox UI. On native desktop: OpenGL / GLFW; on embedded Linux: direct `/dev/fb0`; on the web (WASM): canvas presentation via `lam_canvas_flush`. |
| **MUCK Engine & Simulation** | `axil-nd` | World persistence, room topology, entity simulation, physics, and Binary Client Protocol (`#b` BCP) packet encoding. Contains ZERO UI or DOM code. |

### UI Decoupling Contract
Modders can choose how their area of space is presented without modifying any backend simulation code:
1. **Bud UI Client (`mods/nd/ux/nd_ui.c`)**: Pure-C isomorphic HTML DOM HUD (character vitals, paper doll equipment modal, grimoire, chat/command).
2. **liblamina Client**: Standalone 2D immediate-mode graphics surface for visual tilemaps, sprites, and spatial views.
3. **Custom Native Clients**: Modders can author standalone native C clients for their specific quadrant or territory.

---

## 2. Morton Key Prefix & Bitmask Arithmetic Table

Regions in Xylem and NeverDark are pure bit-prefixes of the 64-bit Morton spacetime keyspace `(id, plen)`. Each bit represents a recursive spatial subdivision:

| Prefix Width (`plen`) | High-Bit Mask | Spatial Scale / Scope | Hierarchy & Modder Governance |
|---|---|---|---|
| `plen = 0` | `0x0000000000000000` | Cosmos (Universe) | Root tier. Server administrator / engine core. Global laws & baseline modules. |
| `plen = 11` | `0xFFE0000000000000` | 32-Planet Super-Sector | Sovereign sector governor ($2^5 = 32$ worlds, covering worlds 0..31). Loads baseline gameplay suite once. |
| `plen = 16` | `0xFFFF000000000000` | Single Planet / World | Planetary ruler ($2^{16} = 65,536$ worlds total). Local planetary laws, biomes, and rules. |
| `plen = 24` | `0xFFFFFF0000000000` | Hemisphere / Continent | Continental governor. Regional mechanics, sub-factions. |
| `plen = 32` | `0xFFFFFFFF00000000` | District / Zone / Area | District magistrate / modder. Area-specific custom graphics (e.g. `liblamina` viewport). |
| `plen = 48` | `0xFFFFFFFFFFFF0000` | City / Settlement | Town mayor / settlement builder. Local vendors, town laws. |
| `plen = 64` | `0xFFFFFFFFFFFFFFFF` | Single Room / Cell | Room architect / individual player room. Custom room scripts and fixtures. |

**Hierarchical Modder Attribution Rule**:
The most powerful modder governs the coarsest prefix (fewer bits = larger spatial domain). Each additional bit refines the area. When events occur at coordinate `code`, XY hierarchical dispatch (`nd_scope_dispatch`) begins at root (`(0, 0)`) and descends down the prefix tree to the leaf. Ancestor modules execute for all descendants automatically, and ancestor `deny` rules strictly bound child regions.

---

## 3. Pluggable Viewport Layout

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

## 4. Viewport Modes

### Mode A: TTY Terminal Viewport (Default)
- **Container Element**: `<div id="term" data-bud-terminal="1">`
- **Render Engine**: `xterm.js` lazily mounted via `createTerminalCapability()`.
- **Data Flow**:
  - Outbound: Keystrokes -> `wasm_term_on_input()` -> `ws_send()` to server.
  - Inbound: Server emits ANSI text -> `wasm_ws_on_message()` -> `term_write()` to xterm.
- **Use Case**: Traditional MUCK/MUD gameplay, command-line inspection (`help`, `man`), and wizard building (`sh`, `loadmod`).

### Mode B: Graphical 2D Viewport via liblamina (2D Tilemaps)
- **Container Element**: `<canvas id="viewport" data-bud-canvas="1" width="640" height="480">`
- **Render Engine**: `liblamina` (`mods/nd/ux/nd_lamina.c` / `nd_lamina.h`).
- **Data Flow**:
  - Inbound: Server emits BCP packets (`#b`).
  - C Parser: `nd_bcp_decode()` updates `nd_client_state_t` and `nd_scene_t`.
  - Render Loop (`wasm_canvas_render` / `wasm_tick`):
    - `nd_lamina_render_scene()` renders tilemaps, dynamic exits, avatars, health bars, and flushes to canvas via `lam_canvas_flush()`.
- **Use Case**: 2D graphical spatial visualization, combat tactics, and custom modded rendering surfaces.

---

## 5. Decentralized Linker Allowlist Architecture

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

## 6. Composable Browser Runtime (`bud-client.js`)

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
