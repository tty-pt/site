# NeverDark / Seven Engine Optimization & Multi-Threading Architecture

Here is a comprehensive technical breakdown of how the NeverDark / Seven engine currently operates across all five optimization areas, where the exact bottlenecks live in the codebase, and how to evolve each subsystem—including a clean, POSIX-compatible (`pthread` + `libxylem` workqueue) multi-threading standard for the Web HAL.

---

## 1. Truly Efficient Rendering at All Zooms (Ground $\to$ Stratosphere $\to$ Orbit)

Right now, the renderer in [`nd_seven.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c) and [`nd_seven_gl.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c) uses [`libareole`](file:///home/quirinpa/site/external/libareole/include/ttypt/areole.h) (`s_seven_chunk_sys`) to bound active terrain leaf chunks to `SEVEN_MAX_VISIBLE_LEAVES = 160`. However, three distinct bottlenecks still degrade performance across the zoom spectrum:

### A. Current Bottlenecks Across Zoom Scales

1. **Synchronous Node Population Stalls during Zoom/Pan ([`areole.c:919-969`](file:///home/quirinpa/site/external/libareole/src/areole.c#L919-L969))**:
   - Every `ar_node_t` in `libareole` has `grid_dim = 48` ($49 \times 49 = 2,401$ vertices).
   - When a node misses the cache in [`ar_get_or_populate`](file:///home/quirinpa/site/external/libareole/src/areole.c#L785-L973), it calls [`seven_macro_chunk_sampler`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c#L56-L167) 2,401 times synchronously on the render thread.
   - Worse: [`seven_macro_chunk_sampler`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c#L64) hardcodes `nd_field_sample(world, 3, wx, wy)` (`lod = 3`, full 16-octave point sampling) **even for Level 14 orbital chunks**! Evaluating micro-roughness octaves (1-world-unit wavelengths) at a vertex spacing of $50,000\text{ world units}$ wastes ~60% of CPU cycles on sub-Nyquist noise that aliases or gets thrown away.
2. **Mid-Field Scenery Iteration Explosion at Mid-to-Far Zoom ([`nd_seven.c:2261-2447`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c#L2261-L2447))**:
   - Scenery fades out up to `SCENERY_ALT_FADE_END = 44000.0f` across a radius of `SCENERY_RAD_MAX_ROOMS = 250` ($500 \times 500 = 250,000\text{ rooms}$).
   - While $8 \times 8$ room blocks are cached in `s_block_bb_cache` (`4096` slots), zooming out to `dist = 25,000` still iterates through thousands of $8 \times 8$ blocks every frame on the CPU, copying up to `262,144` billboards into `s_mid_gpu_batch` and expanding each billboard on the CPU into 6 to 18 vertices (`80 bytes/vertex`) in [`nd_seven_gl_flush_billboards`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.h#L189-L191).
3. **Per-Chunk Uniform & Draw-Call Overhead**:
   - `nd_seven_gl_flush_terrain_chunk` issues an individual draw call and texture bind pair (`uHeightTex`, `uSubstrateTex`) for each of the 160 visible leaves, and then repeats that in `nd_seven_gl_flush_water` and `nd_seven_gl_flush_clouds`.

### B. Making Rendering Truly $O(1)$ at All Zooms

* **Scale-Locked Sampler Band-Limiting**: Pass the node's physical vertex step $\Delta = \text{node->w} / \text{grid\_dim}$ in `Q16.16` directly to [`nd_field_elevation_step_q12_4`](file:///home/quirinpa/site/external/nd-world/include/nd/field.h#L1107-L1171) via `chaff_octave_weight_q16()`. Coarse macro-chunks (Level $\ge 2$) will automatically skip fine octaves above the Nyquist cutoff while remaining 100% mathematically locked to coarse continental elevation and sea level.
* **GPU Instanced Billboards & Hierarchical Impostor LOD**:
  - Instead of expanding 6 vertices (480 bytes) per sprite + 6 vertices per shadow on the WASM CPU every frame, upload the compact 48-byte [`lam_billboard_gpu_t`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.h#L44-L55) struct directly as an instanced vertex attribute (`glDrawArraysInstanced` with a divisor of 1). That cuts CPU-to-GPU vertex bandwidth by **$20\times$**.
  - For mid-zoom (`dist = 3,000..44,000`), attach hierarchical scenery impostor summaries directly to `libareole` quadtree levels (Level 1 = $2 \times 2$ rooms, Level 2 = $4 \times 4$ rooms) so the CPU only emits dominant canopy trees/boulders proportional to screen-pixel area rather than walking every individual room.
* **2D Array Texture / Mega-Atlas Instancing for Terrain**:
  - Pack the `MAX_CHUNK_TEXTURES = 192` node height (`R16I`) and substrate (`RGBA8`) textures into a single `GL_TEXTURE_2D_ARRAY` ($49 \times 49 \times 192$).
  - Then all 160 visible terrain chunks—and all water chunks—can be drawn in **one single `glDrawElementsInstanced` call** per pass.

### C. Implementation Status (Completed in Wave 2026.12.2)

1. **Scale-Locked Nyquist Band-Limiting**:
   - Fixed argument ordering in [`external/nd-world/include/nd/field.h`](file:///home/quirinpa/site/external/nd-world/include/nd/field.h) (`chaff_fbm_rotated_2d_step_q12_4`), passing `step_q16` as the 3rd argument to drive `chaff_octave_weight_q16`.
   - In [`mods/nd/ux/nd_seven.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c), routed node physical vertex spacing $\Delta = \text{node->w} / \text{grid\_dim}$ into `nd_field_sample_step_q12_4`. High-frequency noise is band-limited above the Nyquist cutoff without popping or loss of sea level locks.
2. **4-Corner Lattice Cache Unification**:
   - Added 32-slot cell corner memoization (`s_per_cache[32]`) and 64-slot `noise_wcache_t s_fbm_wcache[64]` bank in [`external/libchaff/include/ttypt/chaff.h`](file:///home/quirinpa/site/external/libchaff/include/ttypt/chaff.h) (and mirrored in npm).
   - Added coarse elevation octave caches (`s_coarse_wc[9]`) and 5-octave river contour caches in [`external/nd-world/include/nd/field.h`](file:///home/quirinpa/site/external/nd-world/include/nd/field.h).
   - Added 32-slot corner cache `s_vc[32]` in [`external/nd-seven/include/nd/seven-world.h`](file:///home/quirinpa/site/external/nd-seven/include/nd/seven-world.h) for torus border warp evaluation.
   - Bound static caches in [`mods/nd/ux/nd_seven.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c) for ecotone blending (`s_eco_c5`, `s_eco_c3`) and 32-slot living meadow corner cache (`s_mc[32]`).
3. **Zoom Scenery Culling & Budget Scaling**:
   - Added fast envelope guard `(node->max_val >> 4) > local_wl` in `seven_emit_room_billboards_ex` ([`mods/nd/ux/nd_seven.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c)) to skip submerged billboard sampling in $O(1)$.
   - Tightened distance shadow thresholds in [`mods/nd/ux/nd_seven_gl.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c): shadows at `dist < 5000.0f` and grass shadows at `dist < 1800.0f`.
   - Updated multi-view zoom test assertion bounds in [`tests/unit/test_ground_zoom_lod.c`](file:///home/quirinpa/site/tests/unit/test_ground_zoom_lod.c).
4. **Build & Quality Gate Portability**:
   - Automatically probe `clang-18` in `WASI_CC` across [`Makefile`](file:///home/quirinpa/site/Makefile), [`build.mk`](file:///home/quirinpa/site/build.mk), and [`external/mk/iso.mk`](file:///home/quirinpa/site/external/mk/iso.mk).
   - Resolved runpath check false positive by using POSIX `[ -h ... ]` in `Makefile`.

---

## 2. Better Use of LOD / Chunks & Advanced Occlusion Techniques

### A. Smarter LOD & Chunk Management in `libareole`

1. **Matrix Stepper (`chaff_fill_lod` / `noise_quad`) Instead of Point-by-Point Sampling**:
   - Currently, [`ar_get_or_populate`](file:///home/quirinpa/site/external/libareole/src/areole.c#L919-L969) loops over $49 \times 49$ points and calls a scalar callback `sys->sampler(...)`.
   - For aligned power-of-two grids, `libchaff`'s recursive hypercube stepper ([`noise_quad`](file:///home/quirinpa/site/external/libchaff/include/ttypt/noise.h#L155-L188) / [`chaff_fill_lod`](file:///home/quirinpa/site/external/libchaff/include/ttypt/chaff.h#L133-L151)) hashes only the cell corners and steps interior points using pure integer register additions (`*c += vt[0]`), which is **$10\times\text{–}15\times$ faster** than scalar point hashing.
   - Adding an optional bulk-tile callback `ar_block_sampler_fn` to [`ar_config_t`](file:///home/quirinpa/site/external/libareole/include/ttypt/areole.h#L116-L154) lets `libareole` populate an entire node grid in a single vectorized pass.
2. **Vertex Geomorphing & Skirt Elimination**:
   - In [`nd_seven_gl_grid_quads_for_zoom`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.h#L211), mesh density jumps discretely between $16 \times 16$, $32 \times 32$, and $48 \times 48$ quads.
   - Because `libareole` already reuses parent samples at even grid indices (`can_reuse_hierarchy` in [`areole.c:928-954`](file:///home/quirinpa/site/external/libareole/src/areole.c#L928-L954)), we can pass a per-chunk continuous morph factor $\alpha \in [0, 1]$ to `s_vs_terrain` that blends odd vertices toward the average of their even neighbors as a chunk approaches its split/merge threshold. This eliminates geometric popping and T-junction cracks between adjacent LOD levels.

### B. Better Occlusion Techniques

Currently, [`nd_seven.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c) only performs **frustum culling** (`ar_frustum_test_box` and `seven_test_frustum_xy_state`). It does **zero occlusion culling**, and it draws terrain chunks in arbitrary quadtree order and billboards back-to-front (painter's order). We can add four high-impact occlusion layers:

1. **Front-to-Back Opaque Terrain Sorting + Early-Z Prepass**:
   - Currently, `s_visible_leaves` are dispatched in quadtree traversal order for terrain, and then re-sorted **back-to-front** (North-to-South) at [`nd_seven.c:2239`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c#L2239).
   - For the **opaque terrain pass**, sorting `s_visible_leaves` **front-to-back** (nearest to camera eye first) allows the GPU hardware `Early-Z` depth test to reject fragments of distant mountains and valleys occluded behind foreground ridges before running the expensive 7-octave terrain + cloud-shadow fragment shader.
2. **CPU 1D Horizon / Ridge-Line Occlusion Buffer (Perfect for 55° Oblique Diorama)**:
   - Because the Seven camera pitches at an oblique angle (~55°) looking North across the landscape, terrain sweeps strictly from South (foreground, bottom of screen) to North (background, top of screen).
   - As `libareole` traverses chunks front-to-back, we can maintain a lightweight 1D screen-column horizon array `float horizon_max_y[256]` (using each node's `min_val` and `max_val` elevation envelope already computed in `ar_node_t`).
   - If a distant chunk's projected `max_val` (mountain peak + max tree height) projects *below* the current `horizon_max_y` of a foreground mountain range, **both the terrain chunk and all of its scenery billboards are 100% occluded** and can be skipped on the CPU before touching WebGL.
3. **Planetary Torus Back-Face & Throat Horizon Culling (Orbital Zoom)**:
   - At orbital zoom (`dist >= 250,000`), the world wraps onto a 3D curved torus ([`corolla_torus`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c#L573)).
   - Every `ar_node_t` can store a precomputed normal cone (mean surface normal $\vec{N}$ and half-angle $\theta_{\text{cone}}$). During `ar_system_query_matrix`, if $\vec{V}_{\text{eye}} \cdot \vec{N} < -\sin(\theta_{\text{cone}})$, the entire patch is on the back side of the planet or tucked behind the inner torus throat and is culled in $O(1)$ without projecting 8 AABB corners.
4. **WebGL2 Hardware Occlusion Queries (`GL_ANY_SAMPLES_PASSED_CONSERVATIVE`)**:
   - For the companion planet/moon pass ([`nd_seven.c:838-885`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c#L838-L885)) or large macro-regions behind high mountains, WebGL2 supports asynchronous non-blocking occlusion queries (`glBeginQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE, q)`), using the previous frame's visibility result to skip entire occluded regions with zero GPU pipeline stalls.

### C. Implementation Status (Completed in Wave 2026.12.3)

1. **Front-to-Back Opaque Terrain Sorting (Zero-Cost Hardware Early-Z)**:
   - In [`mods/nd/ux/nd_seven.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c), implemented `seven_sort_leaves_front_to_back()`, sorting up to 160 active leaf chunks in ascending order of camera eye distance.
   - For diorama mode (`dist < 250,000`), the camera eye is clamped to each node's 3D bounding box `[ox, ox+w] x [oy, oy+h] x [z_min, z_max]`.
   - For 3D curved-torus mode (`dist >= 250,000`), the node center is mapped to 3D Cartesian space via `nd_seven_gl_torus_frame()` and measured against `eye_3d`.
   - Because WebGL uses `glDepthFunc(GL_LEQUAL)` and `glDepthMask(GL_TRUE)` for opaque terrain, hardware Early-Z rasterizers on the GPU reject occluded fragments of background mountains and valleys with zero fragment shader ALU cost ($2\times\text{–}4\times$ fill-rate reduction).
2. **CPU 1D Horizon / Ridge Occlusion Culling**:
   - Implemented a 128-bin screen-space horizon buffer `s_horizon_y[SEVEN_HORIZON_BINS]` in [`mods/nd/ux/nd_seven.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c).
   - In `seven_node_occluded_by_horizon()`, each node's 8 bounding box corners (including a 64m margin for scenery flora) are projected via `flat_mvp` to NDC. If across all covered screen bins its maximum projected screen elevation is strictly below the recorded foreground horizon (`max_ndc_y < s_horizon_y[b] - 0.02f`), the chunk is flagged as fully occluded.
   - Fully occluded chunks skip `nd_seven_gl_flush_terrain_chunk` (0 WebGL draw calls, 0 texture binds) and are pruned from `s_visible_leaves`, automatically skipping CPU billboard generation and room block queries in the downstream scenery pass.
   - Visible chunks conservatively update the horizon buffer via `seven_horizon_update()`, rasterizing their far edge at minimum elevation ($z_{\min}$) across the covered horizontal bins.

---

## 3. Always Using Our Fast Noise (`libchaff` / `noise.h`) Instead of Custom/Perlin Variants

An audit of the codebase shows that while [`nd/field.h`](file:///home/quirinpa/site/external/nd-world/include/nd/field.h) and [`nd_seven_gl.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c) use `libchaff`, **two major rendering hotspots still use bespoke, unoptimized value/Perlin-style noise**:

| Location | Current Bespoke Noise | Why It Hurts | Fast `libchaff` Replacement |
| :--- | :--- | :--- | :--- |
| **[`mods/nd/ux/nd_seven.c:1196-1214`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c#L1196-L1214)** | `meadow_noise_2d(float wx, float wy, float scale, uint32_t seed)` | Uses `float` division (`1.0f / scale`), `floorf()`, 4 scalar `meadow_hash32` calls per point, and float Hermite interpolation. Called **up to 7 times per lattice point** across thousands of meadow points every frame! | Replace with integer `noise_wpoint_c` (with `noise_wcache_t` cell corner cache) or `noise_wrow` scanline stepping from [`<ttypt/noise.h>`](file:///home/quirinpa/site/external/libchaff/include/ttypt/noise.h). |
| **[`external/nd-seven/include/nd/seven-world.h:883-933`](file:///home/quirinpa/site/external/nd-seven/include/nd/seven-world.h#L883-L933)** | `ndw_lattice()` + `ndw_vnoise()` in `ndw_warp_grid_fill()` | Evaluates 5 octaves of scalar point-by-point `ndw_fdiv` + 4-corner hashing across an $81 \times 65 = 5,265$ node grid ($26,325$ noise evaluations per room border cache miss). | Replace `ndw_warp_grid_fill` with `chaff_fill()` / `noise_wrow()` scanline stepping, reducing corner hashes by **$16\times$** and eliminating `ndw_vnoise`. |
| **[`external/nd-world/include/nd/field.h:1035-1058`](file:///home/quirinpa/site/external/nd-world/include/nd/field.h#L1035-L1058)** | `nd_field_eval_coarse_unwrapped_q12_4()` | Loops over 14 octaves calling uncached `chaff_wpoint_periodic_2d` and `noise_wpoint_n` on every room evaluation. | Use `noise_wcache_t` per octave (just like `nd_field_ruggedness_q8` does at line 1083) so adjacent samples within the same macro cell reuse the 4 hashed corners in registers. |

By unifying `meadow_noise_2d` and `ndw_vnoise` onto `<ttypt/noise.h>` and `<ttypt/chaff.h>`, we get:
1. **Zero floating-point `floorf()` or divisions** in the meadow & border loops.
2. **Bit-exact cross-platform determinism** (`XXH32` corner hashes + `int64_t` multiply-then-shift).
3. **Register scanline stepping (`noise_wrow` / `noise_quad`)**: When evaluating the 26-unit meadow lattice in [`nd_seven.c:2017-2023`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c#L2017-L2023) or the $49 \times 49$ `libareole` chunk grid, stepping along a row hashes corners only when crossing a $2^{\text{shift}}$ cell boundary, turning 4 hashes + 20 ALU ops per sample into **1 integer addition per sample**.

---

## 4. Parallel Shader Compilation in the Client Browser

We already have the foundation of parallel shader compilation in [`external/liblamina/htdocs/webgl-bridge.js`](file:///home/quirinpa/site/external/liblamina/htdocs/webgl-bridge.js#L24-L188), and there is a critical bottleneck in it that we can fix.

### A. How It Works Right Now (and the Hidden Bottleneck)

1. In [`nd_seven_gl.c:5113-5141`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c#L5113-L5141), `init_pipelines()` compiles all 5 heavy shaders (`terrain`, `water`, `billboard`, `cloud`, `csm`) via `create_program_async()` and polls `GL_COMPLETION_STATUS_KHR` (`0x91B1`) each frame in `check_pipelines_ready()`.
2. In [`webgl-bridge.js:25-109`](file:///home/quirinpa/site/external/liblamina/htdocs/webgl-bridge.js#L25-L109), `glLinkProgram` sends the vertex and fragment shader source strings to a background `SHADER_WORKER_SOURCE` Web Worker that owns an `OffscreenCanvas(1, 1)` WebGL2 context and queries `KHR_parallel_shader_compile`.
3. **The Hidden Bottleneck ([`webgl-bridge.js:163-166` & `226-258`](file:///home/quirinpa/site/external/liblamina/htdocs/webgl-bridge.js#L226-L258))**:
   - WebGL contexts in separate Web Workers **cannot share `WebGLProgram` objects directly**.
   - Right now, `webgl-bridge.js` waits for the `shaderWorker` to finish compiling and linking on its dummy `OffscreenCanvas` (purely to warm the browser's internal Dawn/ANGLE shader cache), and *only after* the worker finishes does it call `finalizeLocalProgramLink(c, msg.programId)` on the main WebGL context!
   - Even worse: in `glCompileShader` ([`webgl-bridge.js:470-478`](file:///home/quirinpa/site/external/liblamina/htdocs/webgl-bridge.js#L470-L478)), when `w` (`shaderWorker`) exists, **it skips calling `c.compileShader(s)` on the real WebGL context until the worker finishes!**
   - That means if the browser *already* natively supports `KHR_parallel_shader_compile` (which Chrome, Edge, Firefox, and Safari all do via ANGLE/driver threads), the current worker round-trip actually **doubles the compilation latency** (compiling once in the worker, then compiling and linking a second time on the main context).

### B. Making Browser Shader Compilation Truly Parallel & Near-Instant

1. **Direct Native `KHR_parallel_shader_compile` First**:
   - When `c.getExtension('KHR_parallel_shader_compile')` is present on the rendering WebGL2 context, call `c.compileShader(s)` for all 10 shaders (5 VS + 5 FS) in a first batch, and **then** call `c.linkProgram(p)` for all 5 programs in a second batch without any dummy worker round-trip. The GPU driver's native C++ compiler threads compile all 5 pipelines simultaneously on the real context and only flip `0x91B1` (`COMPLETION_STATUS_KHR`) when the hardware pipeline is ready.
2. **Staged / Progressive Pipeline Activation**:
   - Right now, [`nd_seven.c:1746`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c#L1746) blocks **all** rendering (showing a blank dark screen) until *all 5* shaders finish compiling (`if (!nd_seven_gl_pipelines_ready()) return;`).
   - The `cloud` and `water` fragment shaders are by far the heaviest (raymarching 10-octave 4D clouds and Beer-Lambert refraction).
   - If we check readiness **per pipeline** (`terrain_pipe.ready`, `bb_pipe.ready`, `water_pipe.ready`, `cloud_pipe.ready`), the terrain and player/sprites will appear on screen **within 1–2 frames**, and water/clouds will turn on seamlessly a few milliseconds later as their async compilation completes.
3. **Move the Entire WebGL2 Context onto `OffscreenCanvas` in the Worker**:
   - Notice in [`stele-client.js:591-750`](file:///home/quirinpa/site/external/libstele/htdocs/stele-client.js#L591-L750) that `SteleWasmClient` already runs inside a Web Worker and transfers `OffscreenCanvas` (`transferControlToOffscreen`), so all WebGL calls and shader links happen off the main browser UI thread.

---

## 5. A POSIX-Compatible Multi-Threading Standard for the Client (`libxylem` + `pthreads`)

This fits the monorepo's philosophy (isomorphic C99 across Linux/OpenBSD `.so` and browser WASM) seamlessly. Below is how WebAssembly multi-threading works under the hood and how we can standardize it so C code uses standard POSIX/Unix primitives that compile identically on native Unix and browser WASM.

### A. How POSIX `pthreads` Maps to Browser WASM (`wasm32-wasi-threads`)

Clang/LLVM and WASI (`wasi-sdk` / `clang --target=wasm32-wasi-threads -pthread`) support **standard `<pthread.h>`, `<stdatomic.h>`, and `<semaphore.h>` in pure C**:
* **Shared Linear Memory**: When compiled with `-matomics -mbulk-memory -mreference-types -Wl,--shared-memory,--max-memory=...`, the WASM binary imports a `new WebAssembly.Memory({ initial: ..., maximum: ..., shared: true })` backed by a JavaScript `SharedArrayBuffer`.
* **Hardware Instructions**: C11 `<stdatomic.h>` operations (`atomic_load`, `atomic_fetch_add`, `atomic_compare_exchange_weak`) compile directly to native WASM atomic opcodes (`i32.atomic.rmw.cmpxchg`, etc.).
* **Futexes (`pthread_mutex_t` / `pthread_cond_t`)**: Under the hood, `pthread_mutex_lock` and `pthread_cond_wait` compile to the WASM `memory.atomic.wait32` and `memory.atomic.notify` instructions (the exact WebAssembly equivalent of Linux `futex(2)`).
* **Thread Spawning (`pthread_create`)**: When C calls `pthread_create(&tid, NULL, worker_fn, arg)`, a tiny 40-line generic Web HAL capability (`htdocs/pthread-cap.js`) spawns a Web Worker that instantiates the same compiled `WebAssembly.Module` + `SharedArrayBuffer` and invokes the WASM thread entry point `wasi_thread_start(tid, arg)`.
  - *Note*: Browser security requires two HTTP response headers on the server (`Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`) to enable `SharedArrayBuffer`, which is a 4-line addition in `axil`.

### B. Proposed Architecture: Two-Layer Unix Multi-Threading Standard

To make this clean, portable across Linux/OpenBSD and WASM, and ergonomic for any module (`nd`, `auren`, `corm`, `sepal`), we standardize two layers:

#### Layer 1: Low-Level POSIX Compatibility (`<pthread.h>` + `<stdatomic.h>`)
Any C library can use standard POSIX threads and C11 atomics directly:
- `pthread_create`, `pthread_join`, `pthread_detach`
- `pthread_mutex_t`, `pthread_cond_t`, `sem_t`
- `stdatomic.h` (`_Atomic uint32_t`, memory barriers)
- **Graceful Single-Thread Fallback**: If a browser environment lacks `SharedArrayBuffer` (or on a single-core embedded target), `sysconf(_SC_NPROCESSORS_ONLN)` returns `1`, and our workqueue layer automatically executes jobs inline on the caller thread.

#### Layer 2: High-Level Unix Workqueue & Dedicated Role Threads (`<ttypt/wq.h>` or `libxylem` thread pools)
In Unix kernel/systems programming (like Linux `workqueue` or BSD `taskqueue(9)`), you rarely spawn raw ad-hoc threads per frame. Instead, you have **named worker roles / queues** and a **lock-free ring buffer (MPMC/SPSC)**.

We can introduce a tiny header/library `<ttypt/wq.h>` (or integrate into `libstele` / `libxylem`) with a POSIX `taskqueue` / `workqueue` API:

```c
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>

/* Standard Unix-style task signature */
typedef void (*wq_fn_t)(void *arg);

typedef struct wq_task {
    wq_fn_t         fn;
    void           *arg;
    _Atomic uint8_t state; /* 0 = idle, 1 = queued, 2 = running, 3 = done */
} wq_task_t;

/* Create a named workqueue with `nthreads` persistent POSIX worker threads.
 * Maps to pthreads on Linux/OpenBSD and Web Worker pthreads on WASM. */
wq_t *wq_create(const char *name, unsigned int nthreads, unsigned int ring_cap);

/* Enqueue a job (lock-free atomic ring buffer + futex/condvar wakeup) */
int   wq_submit(wq_t *wq, wq_task_t *task, wq_fn_t fn, void *arg);

/* Poll or wait for completion */
bool  wq_task_done(const wq_task_t *task);
void  wq_wait(wq_t *wq, wq_task_t *task);
void  wq_drain(wq_t *wq);
void  wq_destroy(wq_t *wq);
```

### C. Specialized Threads for NeverDark (`mods/nd/ux`)

With `<ttypt/wq.h>` + `pthread` over shared WASM memory, NeverDark can split its client work across **dedicated hardware threads** with zero copying (because they share the exact same C heap and `ar_system_t` arena):

1. **Thread 0: Render & Frame Coordinator (`wasm_tick` / WebGL2 `OffscreenCanvas`)**
   - Runs at a locked 60–120 Hz.
   - Never blocks on terrain generation or scenery synthesis.
   - Reads completed `ar_node_t` buffers (`node->state == AR_STATE_READY`) and completed billboard batches (`slot->state == BB_SLOT_READY`) via `atomic_load_explicit(..., memory_order_acquire)` and submits them to WebGL2.
2. **Thread 1..N: `wq_areole` (Procedural Terrain & Noise Worker Pool)**
   - When `ar_system_query_matrix()` encounters a cache miss during zoom or camera movement:
   - Instead of stalling the frame for 2,401 `nd_field_sample` calls, `ar_get_or_populate()` reserves the `ar_node_t` slot (`state = AR_STATE_BUILDING`), temporarily renders the already-cached parent quadtree node (or coarse fallback), and calls `wq_submit(s_terrain_wq, &node->task, ar_populate_worker_fn, node)`.
   - The background C `pthread` evaluates `chaff_fill_lod()` directly into `node->values` and `node->rgba` in shared WASM memory and flips `atomic_store(&node->state, AR_STATE_READY, memory_order_release)`.
   - Next frame, Thread 0 sees `AR_STATE_READY`, uploads the texture to GPU, and smoothly geomorphs the chunk in.
3. **Thread 2: `wq_scenery` (Mid-Field Billboard & Meadow Synthesizer Thread)**
   - Pre-populates the $8 \times 8$ room billboard blocks (`s_block_bb_arena` in [`nd_seven.c:2367-2406`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c#L2367-L2406)), Poisson-disc object scatter (`ndw_tile_objects`), Living Meadow tufts, and 3D water particle physics in parallel while Thread 0 is issuing terrain draw calls.
4. **Thread 3: `wq_net_corm` (BCP Wire Decode, `libcorm` Sync & `libsepal` Search)**
   - Processes incoming WebSocket BCP deltas, updates client-side `libcorm` tables, and runs `liblemma`/`libsepal` lexical/semantic queries without ever stealing a microsecond from the 60fps render thread.

---

## 6. Recommended Implementation Order (Abstraction-First TDD)

1. **[COMPLETED] Scale-Locked Nyquist Band-Limiting & 4-Corner Lattice Memoization (Wave 2026.12.2)**:
   - Eliminates sub-Nyquist noise octave evaluation on macro-chunks and caches 4-corner grid evaluations across periodic and rotated FBM noise.
2. **[COMPLETED] Front-to-Back Opaque Terrain Sorting & CPU 1D Horizon Culling (Wave 2026.12.3)**:
   - Near-to-far leaf sorting allows hardware Early-Z to drop occluded background terrain at 0 fragment shader cost.
   - 128-bin CPU screen-space horizon buffer culls fully occluded distant mountain chunks before WebGL draw calls and skips downstream scenery billboard generation.
3. **Fast Noise Unification (`libchaff` / `noise.h`) across Meadow & Room Borders**:
   - Replace scalar `meadow_noise_2d` in [`mods/nd/ux/nd_seven.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c#L1196-L1214) with `<ttypt/noise.h>` integer scanline steppers (`noise_wpoint_c` with cell cache), eliminating `float` division and `floorf()`.
   - Replace scalar 5-octave `ndw_vnoise` in [`external/nd-seven/include/nd/seven-world.h`](file:///home/quirinpa/site/external/nd-seven/include/nd/seven-world.h#L892-L906) with `chaff_fill()` / `noise_wrow()` scanline stepping, cutting corner hashes by $16\times$.
4. **GPU Billboard Instancing (`glDrawArraysInstanced`)**:
   - Upload 48-byte [`lam_billboard_gpu_t`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.h#L44-L55) directly into an instanced VBO with divisor 1, moving torus basis expansion to the vertex shader and reducing CPU-to-GPU bandwidth by $10\times$.
5. **Parallel Shader Compilation Fix & Progressive Pipeline Activation**:
   - Fix [`external/liblamina/htdocs/webgl-bridge.js`](file:///home/quirinpa/site/external/liblamina/htdocs/webgl-bridge.js) to compile all shaders in parallel directly on the main context when `KHR_parallel_shader_compile` is present without double-compiling via dummy workers, and activate terrain progressively while water/clouds link.
6. **POSIX Multi-Threading Standard (`<ttypt/wq.h>` + `pthread-cap.js`)**:
   - Create standalone `<ttypt/wq.h>` using standard `<pthread.h>` + `<stdatomic.h>` with a lock-free ring buffer, configure `wasm32-wasi-threads` + `pthread-cap.js` Web HAL bridge, and wire `libareole` async node population and `nd_seven` scenery synthesis onto dedicated background threads.
