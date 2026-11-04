# Unified Client WASM Kernel Architecture for the Pure-C Monorepo

## 1. Executive Summary & Current Diagnostic

The `site` monorepo currently delivers an SSR-first pure-C music platform progressively enhanced with WebAssembly (`mods/*/ux/*.c`). While this ensures No-JS compliance, an audit of the current client-side WASM pipeline reveals substantial structural inefficiencies:

### 1.1 The Multi-Instance & Binary Bloat Problem
Inspecting `htdocs/*.wasm` reveals that every page and UI component compiles its own independent, standalone WebAssembly reactor binary:
- `htdocs/site_chrome.wasm`: **182 KB**
- `htdocs/song_detail.wasm`: **211 KB**
- `htdocs/gig_detail.wasm`: **249 KB**
- `htdocs/list.wasm`: **254 KB**
- `htdocs/chat.wasm`: **185 KB**
- `htdocs/nd.wasm`: **412 KB**
- **Total WASM payload on disk: ~1.5 MB**

On **every single page load**, `htdocs/bud-client.js` executes:
```javascript
// htdocs/bud-client.js line 220:
const settled = await Promise.allSettled(uniqueSpecs.map(loadModule));
```
It concurrently compiles, initializes, and mounts **two independent WebAssembly instances** on the same page:
1. `site_chrome.wasm` (mounted to `#chrome-root`)
2. `[page].wasm` (mounted to `#bud-root`, e.g. `song_detail.wasm`)

This costs nearly **400 KB of WASM transfer** on initial load, instantiates two separate `WebAssembly.Memory` sandboxes, and wastes client CPU re-compiling the same core libraries.

### 1.2 The Browser "C-to-C Communication Wall"
In native C on the server, `libxylem` is the universal module bus: `xy_call()` and `xy_areg()` connect modules across `.so` boundaries with zero glue code.
**In the browser today, Xylem is broken across components:**
- `site_chrome.wasm` has its own isolated `g_xy_hooks` table.
- `song_detail.wasm` has its own separate `g_xy_hooks` table.
- If a user clicks a global search result, a transposition slider, or a theme toggle in `site_chrome`:
  `site_chrome` **cannot** invoke a C hook in `song_detail`! They live in separate WebAssembly memory spaces. They are forced to bounce through the DOM via JavaScript `CustomEvent` or URL reloads.

---

## 2. Cross-Library Unification Scope ("Not Just Libbud")

The single-instantiation kernel model transforms the entire C library ecosystem in the browser:

| Subsystem / Library | Current Monorepo Behavior | Unified WASM Kernel Architecture |
| :--- | :--- | :--- |
| **`libxylem-wasm`** | Isolated hook tables per `.wasm` binary. Zero C-to-C communication between chrome and page content. | **Universal Browser C Bus**: `site_chrome`, `song_detail`, and `gig_list` share one `libxylem-wasm` instance. Pure C `xy_call("on_key_change", +2)` works across components with zero JS glue. |
| **`libbud`** | VDOM diffing and memory arenas duplicated in every `.wasm` binary (~65KB overhead per binary). | **Single VDOM Engine**: One instance diffs `#chrome-root`, `#bud-root`, or arbitrary subtrees in a single memory space. |
| **`libhyle-bud`** | Filter definitions and table formatting logic re-compiled into every list page. | **Shared Schema Bridge**: One compiled filter engine serves song, gig, and group list views dynamically. |
| **`libtransp`** | Statically linked into `song_detail.wasm`. Other pages cannot use chord parsing without re-linking. | **Available Globally in Browser**: Chord transpositions and formatting accessible to any component or modal in C. |
| **`liblemma`** | **Absent from browser**: Lexical search requires full server roundtrips (`/search?q=...`). | **Instant Client-Side Search**: Accent-preserving Unicode case folding (`lemma_fold`) and tokenization run on keystroke in C with zero server latency. |
| **`libcorm` (`cormr`)** | **Absent from browser**: Multi-select filtering roundtrips to axil. | **Instant In-Memory Set Algebra**: Filter 10,000 items in browser RAM via `cormr_set_t` intersections in <0.5ms. |
| **`liblamina`** | Viewport rendering and game state run in an isolated canvas silo. | **Unified Viewport State**: Map coordinates, canvas rendering, and UI chrome share a single coherent memory heap. |

---

## 3. Site Modules & Browser-Side Libxylem Bus

### 3.1 Eliminating DOM Trampolines
With a unified kernel, cross-module communication in the browser becomes 100% pure C:

```c
/* In mods/song/ux/detail.c (Song Detail Component): */
static int handle_transposition_hook(void *ret, void *args) {
    int semitones = *(int *)args;
    g_song_state.transpose += semitones;
    bud_app_set_state(); // Trigger VDOM re-diff and patch
    return 0;
}

void song_detail_init(void) {
    xy_areg("on_song_transpose", &s_transpose_adapter);
}

/* In mods/site_chrome/ux/chrome.c (Global Navigation Bar): */
void on_transpose_button_clicked(int delta) {
    xy_call("on_song_transpose", &delta); // Invokes song detail directly in pure C!
}
```
**Zero JavaScript DOM events. Zero JSON string marshaling. Zero lag.**

---

## 4. Target Architecture: `site_kernel.wasm`

```text
                                Browser Tab
                                     │
                                     ▼
                   htdocs/bud-client.js (Generic Loader)
                                     │
                                     ▼
               [Single WebAssembly Instantiation: ~150KB]
               ┌────────────────────────────────────────┐
               │           site_kernel.wasm             │
               │  ┌──────────────────────────────────┐  │
               │  │ Single Shared WebAssembly.Memory │  │
               │  │ Single Xylem C Hook Registry     │  │
               │  │ Single Bud VDOM Diffing Arena    │  │
               │  └──────────────────────────────────┘  │
               │  Shared Engines:                       │
               │  [bud] [xylem] [hyle-bud] [transp]     │
               │  [lemma] [cormr] [stele]               │
               └──────────────────┬─────────────────────┘
                                  │
         ┌────────────────────────┴────────────────────────┐
         ▼                                                 ▼
   #chrome-root                                       #bud-root
   Hydrated by site_chrome                            Hydrated by song_detail / gig_list
   (Shares C hooks via Xylem)                         (Shares C hooks via Xylem)
```

### 4.1 Strict Invariant Compliance
1. **Preserving Guideline 1 & 5 (SSR is the Contract & No-JS)**:
   The server continues to render 100% complete semantic HTML with `data-*` hooks. No-JS clients see zero difference.
2. **Preserving Guideline 6 (No Site-Specific JavaScript)**:
   `htdocs/bud-client.js` remains strictly generic library infrastructure. It only downloads `site_kernel.wasm` and calls `bud_mount_root(name, selector)`. All domain logic remains in pure C.
3. **Immutable Caching**:
   `site_kernel.wasm` is served with `Cache-Control: public, max-age=31536000, immutable`. The user downloads the ~150KB kernel once across the entire domain. Subsequent page navigations transfer only tiny `bud-state` JSON updates (<5KB).

---

## 5. Non-Disruptive Phasing Roadmap

- **Phase 1 (Preparation)**: Colocate shared WASM reactor compilation in `mk/iso.mk`.
- **Phase 2 (Dual Mounting in Bud)**: Add multi-root mounting support to `libbud` (`bud_app_mount_root(root_name, target_element)`).
- **Phase 3 (Unified Kernel Assembly)**: Compile `site_kernel.wasm` containing `bud`, `xylem-wasm`, `hyle-bud`, `transp`, and `lemma`.
- **Phase 4 (Seamless SPA Transitions)**: Update `bud-client.js` to preserve the warm WASM instance during navigation, updating state via `bud_app_set_state()` without full page reloads.
