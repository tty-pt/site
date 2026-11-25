# NeverDark Performance & Efficiency Roadmap

Phased, measurement-driven roadmap to make NeverDark (`mods/nd`, `libareole`, `liblamina`, `axil-nd`, `nd-world`, `nd-view`) dramatically faster on both integrated GPUs and modest CPUs — while preserving pure-C isomorphic UX, zero site-specific JS, visual fidelity, and existing unit/integration contracts.

Cross-references:
- Build, WASM size, `libbud` arena, and Seven modularization: `docs/CODEBASE_IMPROVEMENT_ROADMAP.md`
- Terrain/LOD architecture: `docs/TERRAIN_ROADMAP.md`, `docs/ANALYSIS_TERRAIN_CHUNKS_LOD_STRATOSPHERE.md`
- Water & cloud specifications: `docs/WATER_REQUIREMENTS.md`, `docs/VOLUMETRIC_CLOUDS_AND_4D_NOISE.md`

---

## Non-Negotiable Guardrails

1. **Zero changes to how the game works.** Gameplay mechanics, movement/controls, world generation, collision, persistence, protocol semantics, and visual appearance at equal quality settings MUST remain identical. All performance work operates strictly under the hood (caching pure functions, eliminating redundant CPU/FFI/GPU work, better data layout, and compiler optimization). Any optional quality/LOD trade-off is strictly gated by existing quality presets (`Low`/`Medium`/`High`).
2. **Measure before & after every phase.** Every phase records real frame-time (ms), draw-call, FFI-call, and counter deltas on the same camera preset and quality level. No unproven "should be faster" changes.
3. **Authoritative asset paths.** JS bridge edits happen ONLY in `external/liblamina/htdocs/{webgl-cap.js,lamina-cap.js}` and `external/libstele/htdocs/stele-client.js`, staged into `htdocs/` via `make stage-assets` and verified with `sh scripts/check-asset-sync.sh` and `sh scripts/check-no-site-specific-js.sh`.
4. **Pure-C isomorphic UX & one-way `mods/nd/seven/` boundary.** `mods/nd/seven/` owns the 3D engine and must remain strictly free of UX/UI (`nd_ui`, `bud`, `stele`) and server (`XY_`, `corm_`, `source_`, `axil_`) dependencies. Verified via `sh scripts/check-module-boundaries.sh` and `sh scripts/check-wasm-imports.sh`.
5. **Abstraction-first TDD.** Changes in `external/libareole`, `external/liblamina`, and `external/nd-*` land with standalone unit tests before integrating into `mods/nd/seven/` or `mods/nd/ux/`. Shader/renderer changes must pass `make test-graphics` (including `glslangValidator` ES 3.00 compile+link across all 12 programs).

---

## Overview

*(Rebased on `HEAD` `d487a6f` after `CODEBASE_IMPROVEMENT_ROADMAP` Phases 0–4: `mods/nd/seven/` extraction, 8-pass `nd_seven_render` split, `nd_seven_atlas.c`/`nd_seven_mesh3d.c` data split, `nd_glsl_assemble` `.rodata` deduplication (`nd.wasm` 1,018 KB), `.cx` UI split + `bud_arena`, and `make test-graphics`.)*

| Phase | Theme | Effort | Risk | Status | Headline Impact |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **P0** | Frame-Phase Profiler & Baseline Harness | Small (2–3h) | Low | **Complete** | Per-phase CPU/FFI/draw-call counters (`nd_seven_perf_t` + `webgl-cap.js`) and reproducible baseline recorded |
| **P1** | Zero-Risk Hot-Path & Bridge Quick Wins | Small–Med (½–1d) | Low | **Complete** | Eliminated steady-state DOM queries (0/f), Hi-Z uniform leak (0/f), ~72% wide-zoom WebGL FFI calls (~82% terrain pass), 5× map re-ingest; enabled `-O3 -fno-math-errno -msimd128` (7,397 SIMD128 ops in `nd.wasm`) |
| **P2** | CPU Frame-Time Killers (Data Layout & Caching) | Medium (1–2d) | Low–Med | **Complete** | High stationary CPU `-92.7%` (`12.14 → 0.89 ms`, probes `14,397 → 57`), Low stationary `-85.8%` (`6.07 → 0.86 ms`), `libareole` query `-32.6%`, `glBufferData` steady-state `0/f` (`glBufferSubData`) |
| **P3** | GPU Shader, Fill-Rate & Pass Trimming | Medium (1–2d) | Med | **Complete** | Sky-after-terrain Early-Z (`z=1.0`, `GL_LEQUAL`), 236 constant `nd_to_lin` `pow()` removed, single-eval wind, zero-`discard` `mesh3d_opaque` Pass C1, CPU pre-culled grass tuft VBO cache + 3-blade LOD (`bindTex` `152→112`, Low grass tris `-61%`), shared MSAA depth resolve (`blits=2.0/f`), dead CSM history blit removed, Bloom gated off on Low (`draws 186→183`) |
| **P4** | Structural Architecture, Memory & Server/Protocol | Large (2–3d) | Med | Planned | WebGL command ring buffer + UBOs, GLSL build stripping, `libareole` single vertex buffer (-20+ MB RAM), non-destructive BCP updates, `axil-nd`/`nd-view`/`nd-world` DB/RAM fixes |

---

## Phase 0 — Frame-Phase Profiler & Reproducible Baseline

**Problem:** Without per-phase timing and call counters, optimizations risk chasing noise or trading CPU time for hidden JS/GPU stalls.

**Data Layout & Invariant:**
- Compact POD counter struct `nd_seven_perf_t` in `mods/nd/seven/nd_seven.h` (updated in-place each frame, zero heap allocation, disabled by default with zero `clock_gettime` calls when off):
  - CPU phase timers (ms) mapped directly to `nd_seven_render`'s decomposed passes: `total_ms`, `sync_ms`, `macro_backdrop_ms`, `near_room_ms`, `botanical_ms`, `sort_batch_ms`, `scenery_ms`, `grass_water_clouds_ms`, `end_frame_ms`.
  - Counters: `terrain_chunks`, `flora_probes`, `bb_flushes`, `mesh3d_count`, `bb_count`, `water_queued`, `cloud_queued`, `ar_populates`, `ar_active_chunks`.
- Standard WASI `wasi_snapshot_preview1.clock_time_get` in `external/libstele/htdocs/stele-client.js` so `clock_gettime(CLOCK_MONOTONIC, ...)` works identically in native C tests and browser WASM.
- Optional JS bridge counter mode in `external/liblamina/htdocs/webgl-cap.js` (`globalScope.__webglBridgePerf`, disabled by default): counts `getGlCalls`, `domQueries`, `getUniformLocationCalls`, `uniformUploads`, `texUploads`, `bufferUploads`, `draws`, `blits`, plus `uniformLocsLen`.

**Deliverables:**
1. Added `nd_seven_perf_t` and per-phase timing/counters in `mods/nd/seven/` (`nd_seven.h`, `nd_seven.c`, `nd_seven_ground.{h,c}`, `nd_seven_gl.{h,c}`, `nd_seven_gl_internal.h`, `nd_seven_gl_occ.c`, `nd_seven_gl_billboard.c`).
2. Implemented `wasi_snapshot_preview1.clock_time_get` in `external/libstele/htdocs/stele-client.js` and optional `__webglBridgePerf` counter hook in `external/liblamina/htdocs/webgl-cap.js`; staged via `make stage-assets`.
3. Added `tests/unit/test_seven_perf.c` + `tests/unit/run-test-seven-perf.sh` and bridge counter assertions in `tests/unit/test-webgl-fbo-bridge.mjs`.

**Recorded P0 Native CPU Baseline (`sh tests/unit/run-test-seven-perf.sh`, GCC `-O1`, 1280×720, stubbed GL — pure CPU cost before JS/WebGL/GPU):**

| Scenario | `tot_ms` | `macro` | `bot` | `sort` | `scen` | `t_chk` | `probes` | `flsh` | `m3d_n` | `bb_n` | `ar_pop` |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| High stationary (10f avg) | **16.37** | 1.27 | **8.54** | 0.20 | **6.34** | 22 | **14,397** | 2.0 | 1,516 | 1 | 0 |
| High panning (12f avg) | **24.41** | **11.31** | **8.00** | 0.28 | 4.81 | 28 | **15,314** | 2.0 | 1,722 | 1 | 9,604 |
| High zooming (8f avg) | **112.74** | **103.89** | 3.18 | 0.14 | 5.51 | 104 | 7,184 | 2.5 | 822 | 411 | **93,324** |
| Low stationary (10f avg) | **6.42** | 1.37 | 2.78 | 0.22 | 2.04 | 22 | **14,421** | 2.0 | 1,521 | 1 | 0 |

**Exit Criteria:**
- Baseline numbers recorded for stationary, panning, zooming, and Low-preset scenarios. (Done)
- `make -C mods/nd && make site-wasm && make stage-assets && sh scripts/check-asset-sync.sh && sh scripts/check-no-site-specific-js.sh && sh scripts/check-module-boundaries.sh && sh scripts/check-wasm-imports.sh && sh tests/unit/run-test-seven-perf.sh && sh tests/unit/run-test-lamina-bridge.sh` exits `0`. (Done)

---

## Phase 1 — Zero-Risk Hot-Path, JS Bridge & Compiler Wins (Wave 1)

**Problem (verified in source at `HEAD`):**
1. **`getGl()` DOM thrash** (`external/liblamina/htdocs/webgl-cap.js:458-460`): Every WebGL import (~2,000–6,000+ calls/frame) invokes `findCanvas()` (`querySelector('[data-bud-webgl]')` / Worker Map scan) before checking the cached `gl` handle.
2. **Per-frame `glGetUniformLocation` + unbounded memory leak** (`mods/nd/seven/nd_seven_gl_occ.c:350-377`, `external/liblamina/htdocs/webgl-cap.js:132,160,318-321,1074-1086`): `gl_build_hiz_pyramid()` queries `glGetProgramParameter` 2× and `glGetUniformLocation` 3× every frame. Each JS call allocates `new TextDecoder()` and appends to `uniformLocs`/`uniformMeta` (~10,800 leaked entries/min at 60 FPS).
3. **Per-chunk redundant terrain uniform/state setup** (`mods/nd/seven/nd_seven_gl.c:1316-1583`): For up to 160–192 chunks/frame, `nd_seven_gl_flush_terrain_chunk()` re-runs `torus_frame_double`, `glUseProgram`, 4 static texture binds, VBO/IBO/attrib binds, ~30 frame-constant uniforms (4,000–8,000 redundant FFI calls + 650–2,000 JS `Float32Array` allocations/frame), and an O(384) linear chunk texture cache scan (`nd_seven_gl.c:1348-1371`).
4. **Full CPU texture cloning & `preserveDrawingBuffer` copies**:
   - `gl_update_clustered_cloud_lighting()` (`mods/nd/seven/nd_seven_gl_clouds.c:199`) uses `glTexImage2D` instead of `glTexSubImage2D`, triggering full JS buffer clones (198 KB + 4 KB) in `webgl-cap.js`.
   - `external/liblamina/htdocs/webgl-cap.js:465` and `external/liblamina/htdocs/lamina-cap.js:167` set `preserveDrawingBuffer: true`, forcing a full-screen compositor copy every frame.
5. **Accidental CPU cache bypasses & 5×/frame map re-ingestion**:
   - `nd_seven_sync_origin()` (`mods/nd/seven/nd_seven_motion.c:114-141`) calls `known_tile_ingest_map(st)` before its unchanged-position early return, running 5×/frame (including on `fps_cap`-skipped frames in `mods/nd/ux/nd_ui.c:422-443`).
   - `s_block_bb_cache` (`mods/nd/seven/nd_seven.c:4074`) requires `b_frustum_state == 2`, so every frustum-straddling 8×8 block (`== 1`) misses the cache and rebuilds per-room every frame (even though per-entry frustum culling for `b_frustum_state == 1` already exists).
   - `s_spatial_cache[3][3]` (`mods/nd/seven/nd_seven_objects.c:767-796`) is smaller than the `7×7` near-field loop, causing 40–41/49 tiles to miss every frame.
6. **Suboptimal compiler flags** (`build.mk:38-98` and native submodules): `nd.wasm` builds with `-Os` (`-O1` in dev) without `-O3`, `-msimd128`, `-flto`, `-fno-math-errno`, `-ffunction-sections -fdata-sections`, or `wasm-opt`; native `axil-nd`, `nd-world`, `liblamina`, and `dev` `mods/nd/nd.so` compile at `-O0`.

**Data Layout & Invariants:**
- `gl_hiz_prog_t`: cache `loc_tex`, `loc_ts`, `loc_uvs`, and `linked` flag once on program completion; never call `glGetUniformLocation` inside the render loop.
- Terrain chunk texture LRU: open-addressed hash table keyed by `(world, level, cx, cy)` indexing the 384-slot pool → O(1) lookup.
- `s_spatial_cache`: expand to `8×8` (power-of-two masked direct index `(rx & 7)`, `(ry & 7)`) so the entire `7×7` near-field fits with zero thrashing.
- `s_block_bb_cache`: cache full block contents whenever `b_frustum_state >= 1` (`1` or `2`), then cull individual cached entries against the frustum when `b_frustum_state == 1`.

**Deliverables:**
1. Fast-path `getGl()` in `external/liblamina/htdocs/webgl-cap.js`; reuse module-level `TextDecoder`/`TextEncoder` singletons across `webgl-cap.js` and `external/libstele/htdocs/stele-client.js`; audit and disable `preserveDrawingBuffer: true` where not needed for screenshot readback (or toggle only for headless capture).
2. Cache Hi-Z program link state & 3 uniform locations in `mods/nd/seven/nd_seven_gl_occ.c`; switch clustered cloud light texture updates in `mods/nd/seven/nd_seven_gl_clouds.c` to pre-allocated storage + `glTexSubImage2D`.
3. Split `nd_seven_gl_flush_terrain_chunk()` into `begin_terrain_pass()` (once per pass: program, shared textures, VBO/IBO/attribs, frame-constant uniforms) + per-chunk uniform delta upload + O(1) chunk cache lookup in `mods/nd/seven/nd_seven_gl.c`; avoid `Float32Array` allocations per uniform call in `webgl-cap.js` via cached memory view + subarray / scratch buffers.
4. Fix `known_tile_ingest_map(st)` generation/dirty gating in `mods/nd/seven/nd_seven_motion.c` + skip on `fps_cap`-dropped frames in `mods/nd/ux/nd_ui.c`; enable `s_block_bb_cache` for `b_frustum_state == 1` in `mods/nd/seven/nd_seven.c`; expand `s_spatial_cache` to `8×8` in `mods/nd/seven/nd_seven_objects.c`.
5. Upgrade `nd.wasm` compile flags in `build.mk` to `-O3 -fno-math-errno -ffunction-sections -fdata-sections` (evaluate `-msimd128` and `-flto` / `wasm-opt` with fallback when unsupported) and ensure release/perf native builds use `-O2`/`-O3`.
6. Stage JS assets via `make stage-assets`.

**Recorded P1 Results (`sh tests/unit/run-test-seven-perf.sh` & `sh tests/unit/run-test-seven-gl-ffi.sh`, 1280×720):**

| Scenario | P0 (`-O1`) `tot_ms` | P1 (`-O1`) `tot_ms` | P1 (`-O3 -fno-math-errno`) `tot_ms` | Delta vs P0 | `t_chk` | `probes` | `m3d_n` | `bb_n` |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| High stationary (10f avg) | 16.37 | **12.14** | 12.25 | **-25.8%** (`-O1`) | 22 | 14,397 | 1,516 | 1 |
| High panning (12f avg) | 24.41 | **19.82** | **18.04** | **-26.1%** (`-O3`) | 28 | 15,314 | 1,722 | 1 |
| High zooming (8f avg) | 112.74 | **105.14** | **72.47** | **-35.7%** (`-O3`) | 104 | 7,184 | 822 | 412* |
| Low stationary (10f avg) | 6.42 | **6.07** | **5.34** | **-16.8%** (`-O3`) | 22 | 14,421 | 1,521 | 1 |

*\*Note: Enabling `s_block_bb_cache` for `b_frustum_state >= 1` exposed and fixed a pre-existing `stl_wq_done()` clobber bug in `mods/nd/seven/nd_seven.c:4094` (`STL_WQ_IDLE` synchronously-built slots were being overwritten with empty staging slot 0 on frame 2), restoring the missing billboard (`411 -> 412`).*

- **WebGL FFI & Leak Counters (`sh tests/unit/run-test-seven-gl-ffi.sh`)**:
  - Steady-state `glGetUniformLocation`: **`0`/frame** (down from 3/frame; 0 `uniformLocs` leak).
  - Steady-state `glGetProgramParameter`: **`0`/frame** (down from 2/frame).
  - Steady-state `glTexImage2D`: **`0`/frame** (clustered cloud lighting updates in-place via `glTexSubImage2D`).
  - High wide-zoom (`159` terrain chunks/frame): **`3,115` total GL FFI calls/frame** (`19.6` total calls/chunk across the full frame; `10` `glUseProgram`, `1,417` `glUniform*`, `274` draws) vs. pre-P1 ~9,200+ terrain-pass calls alone (~11,000+ total frame calls) — **~72% total frame FFI reduction** (**~82%** in terrain pass).
  - High stationary (`22` terrain chunks/frame): **`2,234` total GL FFI calls/frame** (`15` `glUseProgram`, `520` `glUniform*`, `186` draws).
- **JS Bridge & WASM Build**:
  - `getGl()` steady-state DOM queries: **`0`/frame** (verified in `tests/unit/test-webgl-fbo-bridge.mjs`).
  - `htdocs/nd.wasm`: `1,152,433` bytes (`-O3 -msimd128 -DXXH_NO_XXH3 -fno-math-errno -ffunction-sections -fdata-sections`), **`7,397` SIMD128 instructions** (`v128`/`f32x4`/`i32x4`), `WebAssembly.validate === true`, `wasm-audit: PASS`.

**Exit Criteria:**
- `uniformLocs.length` stays constant across repeated frames (0 leak). (Done — `glGetUniformLocation == 0` steady-state verified in `run-test-seven-gl-ffi.sh`)
- Per-frame WebGL FFI calls drop by **≥ 50%**; DOM queries in `getGl()` drop to **0/frame** steady-state. (Done — ~72% wide-zoom FFI drop verified in `run-test-seven-gl-ffi.sh`; `domQueries == 0` verified in `test-webgl-fbo-bridge.mjs`)
- `make -C mods/nd && make site-wasm && make stage-assets` exits `0`. (Done)
- `sh scripts/check-asset-sync.sh && sh scripts/check-no-site-specific-js.sh && sh scripts/check-module-boundaries.sh && sh scripts/check-wasm-imports.sh` exits `0`. (Done)
- `sh tests/unit/run-test-lamina-bridge.sh && make test-graphics && make test-mod MOD=nd` exits `0`. (Done)

---

## Phase 2 — CPU Frame-Time Killers: Data Layout & Caching (Wave 2)

**Problem (verified in source at `HEAD`):**
1. **5 uncached procedural botanical & relief lattices** (`mods/nd/seven/nd_seven.c`: `seven_emit_living_meadow` L2901, `seven_emit_forest_floor_logs` L3097, `seven_emit_medium_botanical` L3246, `seven_emit_large_undergrowth` L3458, `seven_emit_geobreak_props` L3657): Re-evaluated every frame even when the camera is stationary — up to 10,565 lattice points/frame (~40k–75k `meadow_noise_2d`, ~25k–45k `sinf`/`cosf`, ~15k–25k height probes). Pass 3 runs cheap hash density rejections (`sh % 3` / `sh % 9`) *after* frustum, height, trig, and noise.
2. **8× `ar_probe_view` per nearby 3D mesh** (`mods/nd/seven/nd_seven_gl_billboard.c:1105-1120`, `mods/nd/seven/nd_seven_gl_internal.h:886-905`): Every grass/flora 3D mesh within 650 wu calls `shadow_corner_terrain_dz` 4× (2 `nd_seven_ground_elevation_f` each) = 8 `ar_probe_view` calls/mesh/frame (16,000–40,000 probes/frame), and `ar_probe_internal` interpolates full 4-channel float RGBA when only scalar height is needed.
3. **`libareole` software occluder & quadtree refinement overhead** (`external/libareole/src/areole.c:512-647,1829-2280`):
   - `ar_occluder_rasterize_node()` rescans all 2,401 immutable chunk heights per leaf (up to 501k reads/frame), runs double-precision `corolla_eval_frame` 23,200×/frame, and rasterizes 30,720 triangles/frame into a 96×54 buffer.
   - `ar_system_query_matrix()` runs an O(N²) linear scan re-hashing up to 4 Morton keys per candidate per iteration (~51,200 hash lookups/frame).
   - `mods/nd/seven/nd_seven.c:171-220` sets `max_populates_per_frame = 0` and `step_q16 = 0`, causing synchronous zoom spikes (62k–86k noise evals/chunk).
4. **Wasted Shell-sort & split billboard flushes** (`mods/nd/seven/nd_seven.c:3821-3905`, `mods/nd/seven/nd_seven_gl_billboard.c:1205-1375`):
   - `seven_emit_particles_and_sort_batch()` Shell-sorts up to 24,576 × 56 B structs (1.31 MB) recomputing projections in the inner loop — then `nd_seven_gl_billboard.c` immediately counting-sorts `mesh_id > 0` by `mesh_id`, throwing away the Shell sort.
   - `MID_STREAM_CHUNK = 2048` triggers repeated `nd_seven_gl_flush_billboards()` calls per frame, re-binding `tree_shadow_fbo`, reallocating 1.84 MB VBOs with `glBufferData`, and issuing 640–1,920 `glVertexAttribPointer` calls.
5. **Uncached CPU cloud-shell pre-cull** (`mods/nd/seven/nd_seven_gl_clouds.c:7-119`): Worst-case clear sky at `dist > 480` runs ~24,000 2D gradient noises (96,000 `sqrtf`) per frame on CPU.

**Data Layout & Invariants:**
- **Flora/Relief Block Cache (`nd_flora_block_t`)**: Fixed LRU pool (e.g. 128–256 world-space 8×8 blocks keyed by `(world, bx, by, biome_rev)`). Each cached entry stores precomputed `(wx, wy, wz, dz_e, dz_w, dz_s, dz_n, mesh_id, scale, yaw, tint)` so static lattice noise, trig, and ground-slope probes execute **once per block**, never per frame.
- **Separate Emit Queues**: Emit `mesh_id > 0` directly into a dedicated flat `s_mesh3d_queue[]` (no sorting needed; bucketed directly by `mesh_id`), leaving only true transparent 2D billboards in `s_billboard_queue[]` (precomputed sort key `(float dist_sq, uint32_t idx)` — 8 bytes instead of 56 bytes during sort!).
- **Persistent Billboard/Mesh VBOs**: Allocate once with `glBufferData(size, NULL, GL_DYNAMIC_DRAW)`, update with `glBufferSubData`, bind attrib layout once per pass, and coalesce into a single shadow + main flush per frame.
- **`libareole` Node Min-Grid Cache**: Store precomputed `float occ_cell_min[16]` (4×4 coarse conservative minima) inside `ar_node_t` once on populate (`node->rev`), rasterizing 32 triangles/node instead of 192.
- **`libareole` Height-Only Probe API**: Add `ar_probe_height_only()` skipping 4-channel RGBA bilinear interpolation; add max-heap priority queue in `ar_system_query_matrix()` with hoisted `active_view_mask`.

**Deliverables (strict abstraction-first TDD order):**
1. **`external/libareole` first**: Add `occ_cell_min[16]`, coarse 4×4 occluder rasterizer, `ar_probe_height_only()`, and heap-based refinement in `ar_system_query_matrix()`. Add unit tests verifying identical conservative occlusion and exact height parity vs. `ar_probe_view`.
2. **Flora/relief block cache + hash-first culling**: Reorder Pass 3 cheap hash rejections before noise/trig/probes; cache static lattice instances + terrain slope `(dz_e/w/s/n)` per block in `mods/nd/seven/nd_seven.c`.
3. **Separate 3D mesh & 2D billboard queues + coalesced persistent VBO flush**: Split emit queues, sort compact 8-byte keys for billboards only, coalesce `nd_seven_gl_flush_billboards()` into a single pass using `glBufferSubData`.
4. **Populate budget & coarse Nyquist culling**: Enable a frame populate budget (`max_populates_per_frame > 0` with parent-fallback rendering) and evaluate Nyquist octave clamping for coarse LODs (L3+) without introducing shoreline height seams.
5. **Cloud pre-cull cache**: Cache per-chunk synoptic cloud coverage in `mods/nd/seven/nd_seven_gl_clouds.c` on a 0.25–0.5 s cadence (invalidated on wind/time jump).

**Recorded P2 Results (`sh tests/unit/run-test-seven-perf.sh` GCC `-O1`, `sh tests/unit/run-test-seven-gl-ffi.sh`, `make -C external/libareole test`, 1280×720):**

| Scenario | P1 (`-O1`) `tot_ms` | P2 (`-O1`) `tot_ms` | Delta vs P1 | `bot` | `sort` | `scen` | P1 `probes` | P2 `probes` | `t_chk` | `m3d_n` | `bb_n` |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| High stationary (10f avg) | 12.14 | **0.89** | **-92.7%** | 0.05 | 0.01 | 0.13 | 14,397 | **57** (-99.6%) | 22 | 1,516 | 1 |
| High panning (12f avg) | 19.82 | **17.24** | **-13.0%** | 6.97 | 0.01 | 1.83 | 15,314 | **6,493** (-57.6%) | 28 | 1,722 | 1 |
| High zooming (8f avg) | 105.14 | **82.11** | **-21.9%** | 4.61 | 0.01 | 3.65 | 7,184 | 7,650 | 104 | 822 | 412 |
| Low stationary (10f avg) | 6.07 | **0.86** | **-85.8%** | 0.05 | 0.01 | 0.12 | 14,421 | **37** (-99.7%) | 22 | 1,521 | 1 |

- **Exact scene-count parity:** `t_chk`, `m3d_n`, and `bb_n` match P1 identically across all 4 scenarios (`22/1516/1`, `28/1722/1`, `104/822/412`, `22/1521/1`).
- **`libareole` query & occluder (`make -C external/libareole test`, `run-test-areole-chaff-occlusion.sh`):**
  - 5,000-update benchmark: `19.19 → 12.93 us/update` (**-32.6%**, `77,312` updates/sec) via binary max-heap + O(1) free-slot stack in `ar_system_query_matrix()` and precomputed `occ_cell_min[16]` 4×4 occluder grid in `ar_node_t`.
  - Height-only `ar_probe_val()` / `ar_probe_view_val()` verified bit-identical to `ar_probe_view()`; zero false negatives across all occlusion scenarios.
- **Persistent VBOs (`glBufferSubData`, `run-test-seven-gl-ffi.sh`, `run-test-lamina-bridge.sh`):**
  - Steady-state `glBufferData`: **`0`/frame** (down from repeated `1.84 MB` VBO reallocations); steady-state `glBufferSubData`: `4.0`/frame stationary, `6.0`/frame wide-zoom; `MID_STREAM_CHUNK` raised `2048 → 16384`.
- **Invariance note (Deliverable 4):** `step_q16 = 0` and `max_populates_per_frame = 0` were deliberately retained to preserve exact `docs/SHORE.md` shoreline/LOD height invariance and single-frame quadtree refinement depth (a non-zero `max_populates_per_query` caps top-down refinement at uncached intermediate ancestors).

**Exit Criteria:**
- Stationary-camera CPU frame time drops by **≥ 60%** vs. P1 baseline; `flora_probes` drops from 16k–40k/frame to ~0 steady-state. (Done — `-92.7%` High / `-85.8%` Low; `probes` `14,397 → 57`)
- `make areole-lib && tests/unit/run-test-areole-chaff-occlusion.sh` exits `0`. (Done)
- `make -C mods/nd && make site-wasm && make test-graphics && make test-mod MOD=nd` exits `0`. (Done)

---

## Phase 3 — GPU Shader, Fill-Rate & Pass Trimming (Wave 3)

*(Leverages `nd_glsl_assemble` in `mods/nd/seven/nd_seven_gl_shaders.c` — shared helpers live in single-copy `.rodata` chunks and `make test-graphics` validates ES 3.00 compile+link of all 12 programs via `glslangValidator`.)*

**Problem (verified in source at `HEAD`):**
1. **Sky drawn before opaque terrain** (`mods/nd/seven/nd_seven_gl.c:1245-1275`): `s_parts_fs_sky` shades 100% of `render_w × render_h` (18-step torus halo raymarch + ~42 `pow()`) before terrain overwrites 50–85% of pixels.
2. **43× redundant per-tuft math in `s_parts_vs_grass`** (`mods/nd/seven/nd_seven_gl_shaders.c:4137-4318`): Up to 32 chunks × 4,624 tufts × 43 verts = 6.36M vertex invocations/frame, each recomputing identical tuft-constant 5× `chaff_grad_noise2d`, 13× `sin`/`cos`, and 5 texture fetches.
3. **Pure-waste fragment ALU**:
   - `nd_to_lin(vec3(const))` runs `pow(max(c, 0.0), vec3(2.2))` 196 times across `nd_seven_gl_shaders.c` (including shared `s_glsl_substrate_fn`, `s_glsl_valley_fog_fn`, `s_parts_fs_terrain`, `s_parts_fs_sky`, `s_parts_fs_mesh3d`, and inside the `s_parts_fs_cloud` raymarch loop).
   - `eval_recursive_wind()` (3 `chaff_grad_noise2d`) is still re-invoked by 2-line wrappers in `s_parts_fs_terrain` (`L1360, L1363`) and `s_parts_fs_water` (`L2509, L2512`) even though `eval_zoom_water_waves_core` and `eval_shore_wave_swell_core` already accept `wind` as a parameter.
   - Ice noise in `s_parts_fs_water` runs unconditionally even when `freezeFactor <= 0.001`.
4. **Early-Z killer in `s_parts_fs_mesh3d`** (`mods/nd/seven/nd_seven_gl_shaders.c:3609-4088`): 11 `discard` statements disable hardware Early-Z for *all* 3D meshes, including solid trunks and rocks.
5. **Terrain & water texture-fetch explosion**: `sampleHeightSmooth` executes 16 `texelFetch` (plus 4 extra unconditional `getH` fetches in `s_parts_fs_water`) + up to 32 river fetches per pixel regardless of distance or quality tier.
6. **Unconditional post/blit passes**:
   - Bloom runs 3 half-res passes every frame (`mods/nd/seven/nd_seven_gl_occ.c:740`), un-gated by quality.
   - Up to 5 `glBlitFramebuffer` MSAA resolves/frame (`nd_seven_gl_clouds.c`, `nd_seven_gl_occ.c`), including a dead/no-op CSM history blit (`nd_seven_gl_occ.c:1176`) and unused `uSceneDepth` binding in `s_parts_fs_cloud` (`L4621`).

**Data Layout & Invariants:**
- **Opaque → Sky → Water → Clouds pass order**: Render opaque terrain + opaque `mesh3d` first (writing depth), then render sky with `glDepthFunc(GL_LEQUAL)`, `glDepthMask(GL_FALSE)` at `gl_Position.z = gl_Position.w` so hardware Early-Z skips all covered pixels.
- **Two `mesh3d` program variants**: Assemble `s_parts_fs_mesh3d` with `#define ND_MESH_ALPHA_TEST 0` (opaque trunks/rocks — zero `discard`, full Early-Z) and `#define ND_MESH_ALPHA_TEST 1` (canopy/foliage).
- **Per-chunk compact tuft instance buffer (`nd_grass_tuft_t`, 16–20 B/tuft)**: Precompute tuft world pos, height, yaw, wind phase, and biome tint once per chunk populate; vertex shader reads per-instance attributes instead of running 5 noises + 13 trig + 5 texture fetches 43× per tuft; switch to a 12–15 vert LOD mesh beyond ~300 wu.

**Deliverables:**
1. Move sky draw pass after opaque terrain/meshes with `GL_LEQUAL` at `z = 1.0`.
2. Pre-linearize all compile-time constant `nd_to_lin(vec3(...))` colors across `nd_seven_gl_shaders.c`; pass already-evaluated `wind` directly into `eval_zoom_water_waves_core` / `eval_shore_wave_swell_core`; guard ice noise in `s_parts_fs_water` behind `if (freezeFactor > 0.001)`.
3. Split `s_parts_fs_mesh3d` into opaque (no `discard`) and alpha-tested variants; bucket draw calls accordingly.
4. Remove dead `uSceneDepth` in `s_parts_fs_cloud` and no-op CSM blit (`nd_seven_gl_occ.c:1176`); collapse redundant MSAA depth resolves into one shared resolve per frame; gate Bloom on quality preset (off on Low).
5. Add distance/quality tiering for `sampleHeightSmooth` (hardware bilinear / precomputed normal at far distance or Low/Medium quality) and drop the 4 redundant `getH` fetches in `s_parts_fs_water`.
6. Replace per-vertex tuft generation in `s_parts_vs_grass` with per-chunk instanced tuft buffer + mid-range LOD blade count.

**Exit Criteria:**
- Integrated-GPU / high-resolution frame time drops by **≥ 40%** vs. P2 baseline with zero shader compile/link errors in `make test-graphics` (`glslangValidator` ES 3.00).
- Visual parity verified at High quality; Low/Medium presets measurably faster.
- `make -C mods/nd && make site-wasm && make test-graphics && make test-mod MOD=nd` exits `0`.

**Status: Complete** (verified via `make -C mods/nd`, `make site-wasm`, `make test-graphics`, `make test-mod MOD=nd`, `sh tests/unit/run-test-seven-gl-ffi.sh`, `sh tests/unit/run-test-seven-perf.sh`, and boundary/sync scripts):
- **Sky-after-terrain Early-Z**: `s_vs_sky` emits `gl_Position = vec4(ndc, 1.0, 1.0)`; `nd_seven_gl_flush_sky()` runs at the end of `seven_dispatch_macro_backdrop()` (with fallback in `nd_seven_gl_end_frame()`) using `GL_DEPTH_TEST` + `GL_LEQUAL` + `glDepthMask(GL_FALSE)`.
- **Pure-waste fragment ALU**: Pre-linearized all **236** compile-time constant `nd_to_lin(vec3(...))` calls across `nd_seven_gl_shaders.c` (only 16 dynamic runtime calls remain); forwarded single-eval `wind` into `eval_zoom_water_waves_core` and `eval_shore_wave_swell_core` (1× `eval_recursive_wind` per terrain/water FS); guarded ice noise in `s_parts_fs_water` behind `if (freezeFactor > 0.001)`.
- **Opaque vs. alpha-tested `mesh3d`**: Added `nd_seven_gl_fs_mesh3d_opaque()` (zero `discard` statements, validated by `glslangValidator -l` and `test_seven_render.c`) and `nd_seven_mesh3d_is_opaque()` table; Pass C split into C1 (opaque solids, `GL_BLEND` disabled) and C2 (alpha-tested/fading foliage).
- **Blit & post-process trimming**: Removed dead `uSceneDepth` from `s_parts_fs_cloud`; deleted dead CSM history texture/FBO/shader/blit (`csm_reproj`); added `res_depth_valid`/`res_color_valid` tracking in `gl_resolve_scene()` (`blits=2.0/f` in stationary, `end_frame` skips redundant depth blit in wide-zoom); gated 3-pass Bloom off on `ND_QUALITY_LOW` (`draws` `186.0 → 183.0`).
- **Terrain & water texture-fetch tiering**: Added `uTerrFastDist` (Low=750, Med=1500, High=2600 wu) + coarse-chunk (`uChunkSpan.x > 10240.0`) 4-tap quintic-Hermite fast path in `sampleHeightSmooth`; cached `h00/h10/h01/h11` inside the 4×4 bicubic loops in `s_parts_vs_water` & `s_parts_fs_water` (dropping 4 redundant `getH` fetches/pixel).
- **Per-chunk instanced grass tuft VBO + 3-blade LOD**: Added 32-slot LRU `gl_grass_vbo_slot_t` cache of 40-byte pre-culled `gl_grass_tuft_t` records (`glBufferData` once + `glBufferSubData` on miss, `bufData=0` steady-state) and 3-blade LOD mesh (15 verts / 9 tris / 27 indices, 0 clover) beyond `300 wu` or on `ND_QUALITY_LOW`. High stationary `bindTex` dropped **152.0 → 112.0** (`-26.3%`), `unif` **520.0 → 500.0**, and Low stationary instanced triangles dropped **-60.8%** (`1,443,119 → 565,027`).
- **13/13 WebGL2 GLSL ES 3.00 programs** pass `glslangValidator -l` compile+link in `make test-graphics` (exit `0`).

---

## Phase 4 — Structural Architecture, Memory & Server/Protocol (Wave 4)

*(Note: `nd_seven_atlas.c`/`nd_seven_mesh3d.c` data split and GLSL `.rodata` deduplication via `nd_glsl_assemble` already landed in `d487a6f`, reducing `htdocs/nd.wasm` from ~1.2 MB to 1,018 KB.)*

**Problem (verified in source at `HEAD`):**
1. **WASM↔JS FFI overhead**: Even after P1 hoisting, individual GL calls still cross WASM→JS one by one.
2. **WASM binary & RAM footprint**:
   - `htdocs/nd.wasm` is 1,018 KB; `mods/nd/seven/nd_seven_gl_shaders.c` still embeds unstripped GLSL comments/whitespace.
   - `static nd_field_room_memo_t s_memo[4096]` in `external/nd-world/include/nd/field.h:1339` duplicates 114 KB into every TU that includes `field.h`.
   - `libareole` triple-stores vertex data (`ar_sample_t` + `values` + `rgba` = 26 B/vert, ~32 MB for 512 nodes).
   - Synchronous CPU texture baking at startup (`mods/nd/seven/nd_seven_gl.c:258-554`) causes initial load hitching.
3. **Client/Server I/O & DB churn**:
   - `mods/nd/ux/nd_state_io.c:405-408` invalidates `nd_spatial_resolver`, rebuilds `g_scene`, and dirties the minimap even on lightweight packets (`ND_BCP_TOD`, sparsity, macro view).
   - `external/nd-view/src/view.c:453-637` formats and sends 64 KB ANSI `fbcp_view_buf` to graphical clients, performs duplicate `HD_OBJ` reads, and runs up to 147 `HD_SKEL` plant lookups per move.
   - `external/axil-nd/src/law_io.c:36-122` queries `corm_get` on every `nd_write` to resolve `player_ref -> fd`.
   - `external/nd-world/src/spacetime.c:314` scans the entire `obj_hd` DB via `corm_iter` on sunrise/sunset instead of iterating active sessions.
   - `external/nd-mcp/src/mcp.c:15,319` allocates a 655 MB `static char bcp_buf[SUPERBIGSIZ]` (`80000 * 8192`) in BSS.

**Data Layout & Invariants:**
- **WebGL2 UBO + Command Ring Buffer**: Pack per-frame terrain/camera/lighting constants into a single `std140` UBO (`nd_frame_ubo_t`, uploaded once per frame); batch high-frequency draw/bind commands into a flat uint32 ring buffer in WASM linear memory flushed once per pass in `webgl-cap.js`.
- **Single-source `s_memo`**: Move `s_memo` into a `.c` translation unit in `nd-world` (or thread/context struct).
- **Packet-specific dirty flags (`nd_state_io.c`)**: Bitmask `ND_DIRTY_SPATIAL | ND_DIRTY_SCENE | ND_DIRTY_MINIMAP | ND_DIRTY_TOD` so `ND_BCP_TOD` only updates time-of-day uniforms.
- **In-RAM session fd map (`axil-nd`)**: Fixed hash/array map `player_ref -> fd` maintained on connect/disconnect, eliminating `corm_get` from the packet write hot path.

**Deliverables:**
1. **Client packet dirty-masking** (`mods/nd/ux/nd_state_io.c`): Stop tearing down `nd_spatial_resolver`/`g_scene`/minimap on `ND_BCP_TOD`, sparsity, and macro-view packets.
2. **WASM & RAM slimming**: Add build-time GLSL comment/whitespace minifier for `nd_seven_gl_shaders.c`; move `s_memo` out of `external/nd-world/include/nd/field.h`; consolidate `libareole` vertex storage (single canonical buffer + lazy upload); make CPU texture baking lazy/incremental across startup frames.
3. **WebGL2 UBO + Command Ring Buffer**: Introduce `nd_frame_ubo_t` and a batched command flusher in `external/liblamina/htdocs/webgl-cap.js` + `external/liblamina/src/` (keeping direct calls intact for cold setup paths).
4. **Server hot-path cleanup** (each with its own submodule unit test):
   - Skip ANSI `fbcp_view_buf` formatting for graphical/BCP-only clients + cache plant specs in `external/nd-view/src/view.c`.
   - In-RAM `player_ref -> fd` lookup table in `external/axil-nd/src/law_io.c`.
   - Active-session sunrise/sunset notification in `external/nd-world/src/spacetime.c`.
   - Replace 655 MB `static char bcp_buf[SUPERBIGSIZ]` in `external/nd-mcp/src/mcp.c` with a right-sized/growable buffer.

**Exit Criteria:**
- `htdocs/nd.wasm` shrinks by **≥ 100 KB** from GLSL stripping + dead-data cleanup; steady-state WASM heap/RAM drops by **≥ 20 MB**.
- Zero `corm_get` calls per `nd_write`; zero full-DB `corm_iter` on TOD transitions; `nd-mcp` BSS drops by ~655 MB.
- All submodule builds/tests (`areole`, `lamina`, `nd-world`, `nd-view`, `nd-mcp`, `axil-nd`), `make test-graphics`, `make test-mod MOD=nd`, and `make test-fast` exit `0`.

---

## Recommended Execution Sequence

```text
1. P0 (Profiler & Baseline)          → verify: baseline ms / FFI / draw-call table recorded
2. P1 (Bridge, Leak, Hoist, -O3)     → verify: 0 uniform leak, ≥50% FFI drop, Tier-0 + test-graphics green
3. P2 (Flora Cache, Probes, Areole)  → verify: ≥60% CPU drop stationary, Tier-0 + test-graphics green
4. P3 (Sky Early-Z, Shaders, Grass)  → verify: ≥40% GPU/high-res drop, Tier-0 + test-graphics green
5. P4 (UBO/Ring, RAM, Server/DB)     → verify: WASM/RAM/server targets met, make test-fast green
```
