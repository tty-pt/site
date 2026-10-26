# Web HAL Specification: C Systems Web Platform

## 1. Executive Vision

WebAssembly in the browser is not an HTML template engine or a JavaScript helper; it is a **virtual 32-bit CPU operating over linear memory**.

The systems libraries powering this stack—**`corm`**, **`xylem`**, **`lamina`**, **`webgl`**, **`axil`**, and **`bud`**—are isomorphic C99 systems libraries compiled to native POSIX/Linux shared objects (`.so`) for server execution and to `wasm32-wasi` reactor binaries (`.wasm`) for browser execution. The browser environment functions as a **Web Hardware Abstraction Layer (Web HAL)**, exposing direct memory blitting, GPU pipelines, streaming sockets, and accessibility landmarks to pure C.

```
+-----------------------------------------------------------------------------+
|                        ISOMORPHIC C APPLICATION LAYER                       |
|   (Game State, Physics, UI Logic, Data Models, Multi-Modal Viewports in C)  |
+----------------+---------------+----------------+-------------+-------------+
|  libxylem (Bus)|  libcorm (DB) |  liblamina(2D) | webgl (3D)  | libbud (DOM)|
+----------------+---------------+----------------+-------------+-------------+
|                          C RUNTIME & WASM REACTOR                           |
|   (Linear Memory, malloc/free, mmap shims, in-WASM Hook Dispatcher, Tick)   |
+=============================================================================+
|                      THE WEB HAL (CAPABILITY BRIDGES)                       |
+----------------+---------------+----------------+-------------+-------------+
| axil-bridge.js | corm-bridge.js|lamina-bridge.js|webgl-bridge |bud-hydrate  |
| Binary Sockets | Persistence   |Zero-Copy Canvas|GPU Context  |Micro-Patches|
+----------------+---------------+----------------+-------------+-------------+
|                              BROWSER PLATFORM                               |
|   (V8/SpiderMonkey, Canvas2D/WebGL2, WebSockets, DOM Engine, WebAudio)      |
+-----------------------------------------------------------------------------+
```

---

## 2. The 5 Core Systems Primitives

### 2.1 The In-WASM Modular Bus (`libxylem`)
- Native: Dynamically links modules via POSIX `dlopen`.
- WASM: `external/libxylem/src/libxylem-wasm.c` provides an in-memory hook registry (`xy_areg`) and event dispatcher (`xy_call`, `XY_LISTENER_TAG`).
- Invariant: Modules communicate across the Xylem bus without direct symbol dependencies, working identically in native `.so` and browser WASM.

### 2.2 Local State & Relational Queries (`libcorm`)
- Native: Disk/SQLite storage engine and secondary axis indexes.
- WASM: `external/libcorm/src/corm_compat.h` provides WASI linear memory shims (`mmap` anonymous allocations) and host persistence bridges.
- Invariant: Structured records and token arrays are queried and mutated directly in client memory without stringified JSON wrappers.

### 2.3 Direct-to-Screen Graphics (`liblamina` & `webgl`)
- Lamina 2D: Pure-C software rendering directly into linear pixel buffers `uint32_t fb[W * H]`. `htdocs/lamina-bridge.js` blits the buffer via zero-copy `ImageData`.
- WebGL 3D: C calls OpenGL-like bindings (`webgl_draw_arrays`, `webgl_uniform_matrix`) routed to `WebGL2RenderingContext` by `htdocs/webgl-bridge.js`.
- Invariant: Canvas viewports bypass DOM diffing entirely, operating at a clean 60fps.

### 2.4 Multiplexed Binary Network Transport (`axil-ws`)
- `htdocs/axil-bridge.js` maintains a single persistent, auto-reconnecting WebSocket connection multiplexing 3 channels:
  - Stream 0: Raw ANSI text streamed to terminal containers (`#term`).
  - Stream 1: Binary Character Protocol (`#b` BCP frames) carrying state deltas.
  - Stream 2: Remote Procedure Calls (RPC).
- The host passes raw memory pointers to `wasm_ws_on_message()`, where C unpacks binary headers via zero-copy struct casting.

### 2.5 Surgical DOM & SSR Overlay (`libbud`)
- Initial SSR: Emits semantic HTML shells with landmarks (`<header>`, `<nav>`, `<main>`, `<div id="bud-root">`).
- Accessible Overlays: In the browser, Bud manages form inputs, dialogs, and navigation drawers without touching graphics buffers.

---

## 3. The Isomorphic Frame Loop Contract (`wasm_tick`)

Interactive applications execute via a standard 60fps frame loop contract:

```
Browser requestAnimationFrame
  -> wasm_tick(float dt, int width, int height)
     1. Poll & Dispatch Input  -> xy_call(NULL, &on_input_event, &inputs)
     2. Process Network Wire   -> axil_ws_process_backlog() -> BCP unpack
     3. Simulation Step        -> world_update(dt), physics_step(dt)
     4. Render Viewport        -> lamina/webgl direct draw -> lam_canvas_flush()
     5. Surgical HUD Patch     -> if (hud_dirty) bud_patch_text / bud_patch_attr
```

---

## 4. Simplification Strategy: Removing Macro Clutter

1. **Plain C Structs**: State is defined using standard C structs, pointers, and fixed-size buffers. No macro wrappers required for canonical state.
2. **Zero-Copy Binary Wire**: Network deltas stream binary struct layouts directly into WASM linear memory rather than encoding/decoding JSON strings.
3. **Focused Presentation Scope**: Bud is used strictly for declarative DOM elements (`lx_el`, `lx_attr`, `lx_text`, `lx_bind`, `bud_slot`), not for managing game loops or canvas pixels.

---

## 5. Standardized Web HAL Capability Interface

Capabilities are loaded declaratively via `body[data-capabilities]`:

| Capability | Bridge Script | WASM C Header | Primary Responsibility |
| :--- | :--- | :--- | :--- |
| **`axil-ws`** | `htdocs/axil-bridge.js` | `<ttypt/axil-ws.h>` | Binary WebSocket multiplexing, auto-reconnect |
| **`lamina`** | `htdocs/lamina-bridge.js` | `<ttypt/lam.h>` | Zero-copy canvas framebuffer blitting |
| **`webgl`** | `htdocs/webgl-bridge.js` | `<ttypt/webgl.h>` | Hardware-accelerated 3D shader pipeline |
| **`xylem`** | `htdocs/xylem-bridge.js` | `<ttypt/xy.h>` | In-WASM hook registry and event routing |
| **`corm`** | `htdocs/corm-bridge.js` | `<ttypt/corm.h>` | In-memory relational tables & persistence |
| **`bud`** | `htdocs/bud-hydrate.js` | `<bud/bud_app.h>` | SSR hydration, DOM micro-patching |

---

## 6. High-Value Target Usages

1. **Embedded / IoT Web Consoles**: Web servers running on routers/PLCs in <2MB RAM delivering instant SSR and 60fps WASM live telemetry dashboards.
2. **High-Density Data Workstations**: Manipulating arrays of hundreds of thousands of records in client WASM memory using SIMD with zero GC pauses.
3. **Real-Time Multiplayer Worlds**: Seamlessly combining standard HTML/CSS HUD controls with high-performance WebGL 3D scenes and 2D canvas framebuffers.
4. **Offline-First Edge Tools**: Instant SSR initial load, service-worker cached WASM reactor, running 100% client-side offline.
5. **Ultra-Low Latency Public Portals**: Sub-5ms TTFB server responses in pure C, pristine SEO indexability, and progressive WASM enhancement.
