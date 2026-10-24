# Architectural Blueprint: Capability Bridge Distribution, Technical Debt Elimination, and libcorm/libxylem in WebAssembly

**Author:** Technical Architecture Review  
**Date:** October 2026  
**Scope:** `site`, `axil-nd`, `liblamina`, `axil-tty`, `libcorm`, `libxylem`, and tenant game modules.

---

## 1. Executive Summary

This architectural blueprint addresses the distribution of browser capability bridges, the elimination of residual technical debt in `axil-nd`'s HTTP subsystem, and presents a comprehensive feasibility analysis and implementation roadmap for running `libcorm` and `libxylem` within WebAssembly (WASM).

By aligning capability bridges with their owner repositories, replacing static fallback HTML with dynamic isomorphic serializers, and porting Corm/Xylem primitives to WASM, the stack achieves complete parity between server-side native C and browser-side client execution.

---

## 2. Capability Bridge Distribution Architecture

### A. Current State & Deficiencies
- `axil-tty-bridge.js` is correctly co-located in `external/axil-tty/htdocs/axil-tty-bridge.js` and synced to `htdocs/` via `Makefile assets-sync`.
- `lamina-bridge.js` is correctly co-located in `external/liblamina/htdocs/lamina-bridge.js`.
- `webgl-bridge.js` and `<ttypt/webgl.h>` were initially staged directly in the top-level `site` repo (`htdocs/webgl-bridge.js` and `include/ttypt/webgl.h`). Because `htdocs/*` is gitignored by default, `webgl-bridge.js` was untracked, creating a deployment and packaging anomaly.

### B. Co-Location Plan for `liblamina`
`liblamina` is the graphics layer responsible for 2D immediate-mode rendering, tilemaps, fonts, and WebGL rendering contexts. Therefore, both the C headers and browser bridge scripts for WebGL belong inside `liblamina`:
1. **Header Co-Location:**
   - Move `include/ttypt/webgl.h` $\rightarrow$ `external/liblamina/include/ttypt/webgl.h` (and upstream `/home/quirinpa/liblamina/include/ttypt/webgl.h`).
   - `liblamina` now exports both `<ttypt/lam.h>` and `<ttypt/webgl.h>`.
2. **Bridge Co-Location:**
   - Move `htdocs/webgl-bridge.js` $\rightarrow$ `external/liblamina/htdocs/webgl-bridge.js` (and upstream `/home/quirinpa/liblamina/htdocs/webgl-bridge.js`).
3. **WASM Import Allowlist:**
   - Merge `include/ttypt/webgl-wasm-allowed-imports.lst` into `external/liblamina/wasm-allowed-imports.lst`. When a module compiles against `liblamina`, both `lamina.*` and `webgl.*` imports are permitted automatically.
4. **Top-Level `Makefile` Synchronization (`assets-sync`):**
   ```makefile
   assets-sync:
   	@if [ -f external/axil-tty/htdocs/axil-tty-bridge.js ]; then \
   		cp external/axil-tty/htdocs/axil-tty-bridge.js htdocs/axil-tty-bridge.js; \
   	fi
   	@if [ -f external/liblamina/htdocs/lamina-bridge.js ]; then \
   		cp external/liblamina/htdocs/lamina-bridge.js htdocs/lamina-bridge.js; \
   	fi
   	@if [ -f external/liblamina/htdocs/webgl-bridge.js ]; then \
   		cp external/liblamina/htdocs/webgl-bridge.js htdocs/webgl-bridge.js; \
   	fi
   ```

---

## 3. Eliminating Standalone SSR Fallback Technical Debt in `axil-nd`

### A. The Technical Debt in `src/http.c`
In `external/axil-nd/src/http.c`, when `handle_nd` processes a non-WebSocket `GET /nd` and `nd_ssr_respond` is unhooked (i.e. standalone MUCK runs without `mods/nd`), the engine emits `nd_fallback_ssr_html[]`:
- **Flaw 1:** Hardcoded HTML text inside a C string literal (45 lines).
- **Flaw 2:** Silent drift risk: changes to element classes, layout containers, or state JSON in `nd_ui.c` do not reflect in the fallback string.
- **Flaw 3:** Violates the pure isomorphic Bud invariant that all HTML must be generated from component trees.

### B. Concrete Elimination Plan: Standalone Dynamic Bud Serializer
`libbud` is a pure C, zero-dependency library (~15KB). By linking `libbud` directly into `libaxil-nd.so`, `axil-nd` can dynamically generate its fallback DOM tree at runtime:
1. **Dynamic Fallback Function:**
   ```c
   static void nd_dynamic_fallback_ssr(socket_t fd)
   {
       bud_node *vport = bud_element("div");
       bud_set_attr(vport, "id", "viewport");
       bud_set_attr(vport, "class", "flex-1 h-full min-h-[400px]");
       bud_node *term = bud_element("div");
       bud_set_attr(term, "id", "term");
       bud_set_attr(term, "data-bud-terminal", "1");
       bud_set_attr(term, "class", "w-full h-full font-mono bg-[#0a0a0c] text-neutral-200 p-2 rounded border border-neutral-800 overflow-hidden");
       bud_append(vport, term);

       /* Minimal shell using the exact same bud builder */
       bud_node *root = bud_element("div");
       bud_set_attr(root, "id", "bud-root");
       bud_set_attr(root, "data-capabilities", "axil-ws tty lamina bud");
       bud_append(root, vport);

       char *html = bud_render_html(root);
       char resp[BUFSIZ];
       snprintf(resp, sizeof(resp),
           "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
           "<title>NeverDark</title><link rel=\"stylesheet\" href=\"/styles.css\">"
           "<script type=\"application/json\" id=\"bud-state\">{\"me\":0}</script>"
           "</head><body class=\"bg-[#0a0a0c] text-neutral-200\">%s"
           "<script type=\"module\" src=\"/bud-client.js\"></script></body></html>",
           html ? html : "");
       
       axil_header_set(fd, "Content-Type", "text/html; charset=utf-8");
       axil_respond(fd, 200, resp);
       if (html) bud_free_string(html);
       bud_free(root);
   }
   ```
2. **Outcome:** Zero hardcoded static HTML snapshots. Standalone runs and integrated site runs share the identical `bud_node` tree builder semantics.

---

## 4. Feasibility Analysis & Blueprint: `libcorm` in WebAssembly

### A. Core Engine Profile
`libcorm` is a high-performance record and key-value store. In native Linux/macOS, it maps files to memory using `mmap()` / `msync()`, indexes keys using `xxhash`, and stores variable-length records in aligned slices (`idm.c`, `rec.c`, `rec_axis.c`).

### B. WASM Architectural Constraints & Adaptations
1. **Memory Model (`mmap` $\rightarrow$ Linear Memory Arena):**
   - WebAssembly does not provide OS-level `mmap`.
   - *Adaptation:* `libcorm` in WASM operates over a contiguous block allocated from WASM linear memory (`malloc()` or dedicated `WebAssembly.Memory` pages).
   - Because `libcorm`'s internal layout is pointer-offset based (`uint32_t` bucket indices, relative record offsets), all in-memory structures work verbatim in 32-bit WASM memory!
2. **Persistence Architecture:**
   - **Main Thread (Snapshot / Sync):** On state modification or before page unload, `libcorm`'s buffer is dumped to browser `IndexedDB` via a JS bridge function (`corm_host_persist(ptr, size)`).
   - **Web Worker (High-Performance OPFS Sync):** When running in a Web Worker, browser WASM has access to the **Origin Private File System (OPFS)** via `FileSystemSyncAccessHandle`. This provides synchronous `read()` and `write()` calls at near-native NVMe speeds, enabling real `msync()`-style durability without blocking the main UI thread!
3. **Use Cases for the Pure-C Stack:**
   - **Client-Side Game Database:** In NeverDark, instead of parsing thousands of BCP JSON objects, the client maintains a local Corm database of rooms, items, and player records.
   - **Instant Offline Search & Filter:** Site list views (`song`, `gig`, `poem`) can load a Corm binary snapshot into WASM, performing sub-millisecond FTS, sorting, and multi-axis filtering entirely client-side.

---

## 5. Feasibility Analysis & Blueprint: `libxylem` in WebAssembly

### A. Core Engine Profile
`libxylem` provides cross-module hook dispatch (`XY_DECL` / `XY_IMPL`), hierarchical execution regions (`xy_with_region`, `xy_claim_at`), and dynamic module management (`xy_load`).

### B. Two-Stage WASM Implementation Plan

#### Stage 1: In-WASM Hook Dispatch & Region Bus (Static Reactor)
- `src/libxylem-dispatch.c` and `src/libxylem-runtime.c` contain **zero `dlopen` dependencies**.
- They implement pure in-memory data structures: adapter hash tables, listener arrays, and region prefix trees.
- *Feasibility:* 100% portable to `wasm32-wasi`.
- *Architecture:*
  - Compiling `libxylem-dispatch.c` into `nd.wasm` (or any site WASM module) activates the full `XY_DECL` / `XY_IMPL` hook mechanism inside the browser.
  - Client modules (`nd_ui.c`, `nd_attr_ui.c`, `nd_equip_ui.c`) communicate over the native XY bus rather than ad-hoc C function pointers or weak symbols.

#### Stage 2: True Isomorphic Dynamic Module Loading (`xy_load` in WASM)
- In native C, `xy_load("foo")` calls `dlopen("libfoo.so")` and runs `xy_install()`.
- In browser WASM, dynamic loading translates directly to `WebAssembly.instantiate`:
  1. `xy_load("nd-spell")` in C calls a host bridge `wasm_xy_load_module("nd-spell")`.
  2. The browser fetch loads `/nd-spell.wasm`.
  3. `WebAssembly.instantiate` links the child WASM module using the host's `WebAssembly.Table` (indirect function table) and `WebAssembly.Memory`.
  4. The host invokes the child module's `xy_install()` export.
  5. The child module registers its `XY_IMPL` hooks into the shared Xylem dispatch table.
- *Impact:* Game modules (`nd-equip.wasm`, `nd-spell.wasm`, `nd-craft.wasm`) can be dynamically streamed, loaded, and unloaded into the live running client on demand.

---

## 6. Phased Implementation Roadmap

```
  +-----------------------------------------------------------------------------+
  | Phase 1: Bridge Distribution (liblamina & axil-tty)                          |
  | - Move webgl-bridge.js & webgl.h to external/liblamina                      |
  | - Update Makefile assets-sync rule                                          |
  | - Merge wasm-allowed-imports.lst                                            |
  +-----------------------------------------------------------------------------+
                                       │
                                       ▼
  +-----------------------------------------------------------------------------+
  | Phase 2: Eliminate Fallback SSR Technical Debt                              |
  | - Link libbud into libaxil-nd.so                                            |
  | - Replace nd_fallback_ssr_html[] with dynamic bud_node tree builder        |
  | - Verify full standalone daemon compatibility (./test.sh)                   |
  +-----------------------------------------------------------------------------+
                                       │
                                       ▼
  +-----------------------------------------------------------------------------+
  | Phase 3: libcorm in WebAssembly                                             |
  | - Implement memory-mapped linear buffer backend (no-mmap profile)           |
  | - Add IndexedDB / Web Worker OPFS snapshot adapter                          |
  | - Integrate with nd client for binary entity caching                        |
  +-----------------------------------------------------------------------------+
                                       │
                                       ▼
  +-----------------------------------------------------------------------------+
  | Phase 4: libxylem in WebAssembly                                            |
  | - Stage 4A: Static in-WASM XY hook dispatch (nd.wasm)                       |
  | - Stage 4B: Dynamic browser module loading (xy_load -> fetch/instantiate)   |
  | - Full isomorphic module lifecycle across server and client                 |
  +-----------------------------------------------------------------------------+
```

This roadmap establishes a concrete, pure-C path toward total isomorphism across rendering, storage, and modular extensibility.
