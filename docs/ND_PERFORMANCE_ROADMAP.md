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
| **P4** | Structural Architecture, Memory & Server/Protocol | Large (2–3d) | Med | **Complete** | `htdocs/nd.wasm` `-100.7 KB` (`1,172,744 → 1,072,053 B`), `libareole` `-20.52 MB` RAM, `NdFrameUBO` + `glCmdFlush` ring (`FFI -68.5%` stationary / `-82.8%` wide-zoom `3.4 calls/chunk`, live `ulLen=147`), BCP dirty-masking, incremental atlas bake, `0 corm_get` per `nd_write`, `libnd-mcp.so` BSS `655 MB → 264 B`, active-session TOD, graphical ANSI skip + plant cache |
| **P5** | Zero-Spill Integrated GPU & Hitch-Free Zoom | Medium (1–2d) | Med | **Complete** | 3 cloud + 7 terrain zero-visual-diff shader variants + deferred Stage 8 water pass (`CLOUD_WIDE_CSM` `5.24×` fewer cycles / `31.5×` fewer fills; `TERRAIN` `24.86ms` + `WATER` `9.71ms` on Intel ADL GT2, `21/21` `glslangValidator -l` pass), startup `gl_prewarm_pipelines()` + `glTexSubImage2D` chunk reuse (`zoom_sweep max_ms` `4507 → 182.66ms`, `wide_zoom_steady` `24.4 fps` / `35.51ms` GPU) |
| **P6** | Hardware-Filtered Shading, Fast Boot & SIMD Streaming | Large (4–6d) | Med | **Planned** | Target **20 → 45–60 FPS** on Intel ADL GT2 (`TERRAIN` 25→8–12ms, `WATER` 10→3–4ms, `CLOUDS` 7.8→2–3ms), cold boot **14.1s → ~2–3s**, zoom CPU spikes **82ms → ~20ms**, wide-zoom chunk draws **159 → ~5** |

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
- `htdocs/nd.wasm` shrinks by **≥ 100 KB** from GLSL stripping + dead-data cleanup; steady-state WASM heap/RAM drops by **≥ 20 MB**. (Done — `nd.wasm` `-100,691 B` (`1,172,744 → 1,072,053 B`); `libareole` arena `-20.52 MB`)
- Zero `corm_get` calls per `nd_write`; zero full-DB `corm_iter` on TOD transitions; `nd-mcp` BSS drops by ~655 MB. (Done — `0 corm_get`, active `HD_FD` TOD iteration, `libnd-mcp.so` BSS `264 B`)
- All submodule builds/tests (`areole`, `lamina`, `nd-world`, `nd-view`, `nd-mcp`, `axil-nd`), `make test-graphics`, `make test-mod MOD=nd`, and `make test-fast` exit `0`. (Done — note pre-existing `scripts/check-no-tree-rpath.sh` failure on unrelated `external/libjarvis/lib/libjarvis.so`; all unit/standalone/graphics/module/bridge/boundary/WASM suites exit `0`)

**Status: Complete** (verified across native C, WebGL2 FFI harness, submodule suites, boundary checks, and live headless Chrome ANGLE/WebGL2):
- **Client packet dirty-masking** (`mods/nd/ux/nd_state_io.c`): Added `nd_bcp_opcode_dirty_mask()` (`ND_DIRTY_SPATIAL | ND_DIRTY_SCENE | ND_DIRTY_MINIMAP | ND_DIRTY_UI`), returning `0` for `ND_BCP_TOD`, `ND_BCP_SPARSITY`, and `ND_BCP_MACRO_VIEW` so lightweight telemetry frames never tear down `nd_spatial_resolver`, `g_scene`, or the minimap.
- **WASM & RAM slimming**:
  - Build-time GLSL comment/whitespace minifier (`scripts/minify-glsl-c.py`, invoked via `python3 -B` in `mods/nd/Makefile`) shrinks `nd_seven_gl_shaders.gen.c` by `-99,888 B` (`341,848 → 241,960 B`); combined with compact `noinline` command ring helpers, `htdocs/nd.wasm` drops **`-100,691 B` (`-100.7 KB`, `1,172,744 → 1,072,053 B`)**, `WebAssembly.validate === true`, `wasm-audit: PASS`, `8,129` SIMD128 ops.
  - Moved `static nd_field_room_memo_t s_memo[4096]` (114 KB/TU) out of `external/nd-world/include/nd/field.h` into single TU `g_nd_field_room_memo[4096]` under `-DND_FIELD_MEMO_EXTERN`.
  - Bounded `libareole` `ar_sample_t` sub-pool (`samples_pool_capacity = 64`, `samples_max_level = 2`, `ar_stats_t.arena_bytes`) cuts 512-node arena RAM from **`32,112,264 → 10,598,752 B` (`-20.52 MB`)**, verified in `make -C external/libareole test`.
  - Incremental 3-stage CPU procedural atlas baking (`gl_advance_atlas_bake()`, `atlas_bake_stage` `0→1→2→3` across startup frames) in `mods/nd/seven/nd_seven_gl.c`.
- **WebGL2 `NdFrameUBO` + Flat `uint32` Command Ring (`glCmdFlush`)**:
  - Packed shared per-frame uniforms into `std140` `NdFrameUBO` (binding 0) across all 13 `glslangValidator -l` ES 3.00 programs; added `glBindBufferBase`, `glGetUniformBlockIndex`, `glUniformBlockBinding`, `glCmdFlush`, and `lam_gl_cmd_buf_t` in `external/liblamina` (JS + native stub + allowlist) and batched terrain, grass, water, shadow, billboard/mesh3d, and cloud draws in `mods/nd/seven/`.
  - **WebGL FFI (`sh tests/unit/run-test-seven-gl-ffi.sh`):**
    - High stationary (10f avg): **`717.0` total FFI calls/f** (down from `2,279.0` in P3, **`-68.5%`**), `unif=382.0`, `draws=186.0`, `blits=2.0`, `cmdFlushes=9.0`, `cmdOps=1,455.0`, `getUnifLoc=0`, `texImg2D=0`, `bufData=0`, `bufSub=5.0`.
    - Low stationary (5f avg): **`693.0` total FFI calls/f**, `draws=183.0`, `instTris=565,027`, `cmdFlushes=9.0`, `cmdOps=1,451.0`, `bufData=0`.
    - High wide-zoom (5f avg, 159 chunks): **`537.0` total FFI calls/f** (down from `3,115.0` in P1, **`-82.8%`**), **`3.4` calls/chunk** (down from `19.6`), `cmdFlushes=4.0`, `cmdOps=2,331.0`, `getUnifLoc=0`, `texImg2D=0`, `bufData=0`.
  - **Live Headless Chrome ANGLE/WebGL2 (Default/High & Low):** `0` console/page/WebGL errors, `15/15` compiled programs, `74` textures, `domQ=0`, `getUL=+0`, `ulLen=147` (flat steady-state, **`-41.2%`** vs P3's `250`), `+346` draws / `+3` blits / `+12` `cmdFlushes` batching `+2,844` `cmdOps` per steady-state frame.
- **Server hot-path & RAM cleanup** (`sh tests/unit/run-test-nd-phase4a.sh`):
  - `external/axil-nd/src/law_io.c`: Open-addressed `pfd_map[2048]` with backward-shift deletion + `nd_player_is_ws()` → **`0` `corm_get` per `nd_write`** across 200 sessions + collision chains + detach.
  - `external/nd-mcp/src/mcp.c`: Replaced 655 MB `static char bcp_buf[SUPERBIGSIZ]` BSS with 8 KB stack `ND_MCP_OBS_BUFSIZ` → **`libnd-mcp.so` BSS = `264 B`**.
  - `external/nd-world/src/spacetime.c`: `st_update()` iterates active `HD_FD` sessions instead of full `obj_hd` `corm_iter`.
  - `external/nd-view/src/view.c`: Added 256-slot `s_plant_spec_cache`, merged duplicate `HD_OBJ` tile lookup, and skipped 64 KB ANSI `view_draw()`/`fbcp_view_buf()` for graphical WebSocket clients.

---

## Phase 5 — Zero-Spill Integrated GPU & Hitch-Free Zoom

**Problem (verified on real Intel ADL GT2 / Mesa OpenGL ES 3.2 via `INTEL_DEBUG=fs` and `scripts/bench-nd-browser.mjs`):**
1. **Heavy Gen12 register spilling in monolithic terrain & cloud fragment shaders**:
   - `TERRAIN_FULL`: `22,027` SIMD8 instructions, `1,081,161` cycles, **`112:237` spills:fills** — every wet near/mid chunk paid the full 112-spill monolithic shader (~60–72 ms on Intel ADL GT2).
   - `CLOUD_FULL`: `14,064` SIMD8 instructions, `386,147` cycles, **`81:189` spills:fills** — wide-zoom cloud shells spent nearly half as much GPU time as terrain (`CLOUDS/TERRAIN = 0.488`).
2. **Multi-second mid-zoom & first-draw hitches**:
   - Mesa Iris defers NIR → GEN12 machine-code compilation until the first draw per `(program, FBO format)` pair, causing multi-second stalls (`1,406 ms` in `stationary_1x`, `4,507 ms` in `zoom_sweep`) the first time LOD/zoom crosses into a new terrain or cloud variant.
   - Evicted `gl_chunk_tex_t` cache slots re-called `glTexImage2D` on `height_tex` and every `substrate_tex` mip level (~70 chunks × 8 levels during zoom sweeps), churning GPU texture storage.

**Data Layout & Invariants:**
- **3 Zero-Visual-Diff Cloud Shader Variants (`nd_cloud_var_t`)**: `FULL`, `WIDE`, `WIDE_CSM` (compile-time macro specialization stripping unused near-field/analytic-shadow branches without altering rendered pixels).
- **7 Zero-Visual-Diff Terrain Shader Variants (`nd_terr_var_t`)**: `FULL`, `DRY_NEAR`, `DRY_CSM`, `SEABED_NEAR`, `SEABED_MID`, `MACRO`, `MACRO_FAR`. Close/mid wet chunks (`cur_dist <= 2000.0f`, `!water_occluded`, room in `water_queue`) render seabed only (`SEABED_NEAR` / `SEABED_MID`), hoist Stage 7 benthic shading ahead of Stage 6 to shorten live ranges, and encode a 3-state scene alpha contract (`> 0.75` dry/grass/sky/shaded, `0.25..0.75` seabed awaiting Stage 8 ocean optics, `< 0.25` submerged sprite/mesh). Pass B 3D mesh ground shadows mask alpha writes (`glColorMask(T, T, T, F)`) to preserve the contract.
- **Deferred Stage 8 Ocean Optics in `s_parts_fs_water`**: Lifts shoreline vertices smoothly up to `+2.2 wu` (`vSurfRaise`) and projects back along the view ray (`surfXY = vWorldPos - viewHoriz * raisePath`, `trueSurfDist = vDistance + raisePath`) with dynamic-resolution-safe `uSceneUvScale` NDC reconstruction (`nd_scene_dist(sUV)` on `GL_DEPTH32F_STENCIL8`), preserving `docs/SHORE.md` parity while immediately discarding dry pixels on line 1.
- **Hitch-Free Startup & Reuse**:
  - `gl_prewarm_pipelines()` executes once on the first ready frame right before the first `glClear()` using a `1×1` viewport into exact `scene_fbo` (`RGBA16F` MSAA) and `cloud_fbo` (`RGBA16F` 1×) states for all 7 terrain variants, `water_pipe`, and all 3 cloud variants.
  - `gl_chunk_tex_t` tracks `alloc_w`, `alloc_h`, and `alloc_mips`, switching steady-state cache slot reuse to `glTexSubImage2D` across `height_tex` and the full `substrate_tex` mip chain.

**Recorded Mesa `INTEL_DEBUG=fs` Shader Compilation Metrics (Intel ADL GT2, SIMD8):**

| Program | Insts | Loops | Cycles | Spills:Fills | Delta vs Monolithic |
| :--- | ---: | ---: | ---: | ---: | :--- |
| `CLOUD_FULL` (baseline) | 14,064 | — | 386,147 | 81:189 | baseline |
| **`CLOUD_WIDE_CSM`** | **7,124** | — | **73,719** | **3:6** | **5.24× fewer cycles, 31.5× fewer fills** |
| `TERRAIN_FULL` (fallback) | 22,027 | 9 | 1,081,161 | 112:237 | baseline |
| **`TERRAIN_DRY_NEAR`** | **6,602** | 2 | **88,190** | **0:0** | **zero spills, 3.3× fewer insts** |
| **`TERRAIN_DRY_CSM`** | **9,089** | 7 | 997,932 | **15:11** | **21.5× fewer fills** |
| **`TERRAIN_SEABED_NEAR`** | **7,460** | 2 | **123,146** | **16:20** | **11.8× fewer fills, 3.0× fewer insts** |
| **`TERRAIN_SEABED_MID`** | **10,044** | 7 | 1,028,877 | **29:47** | **5.0× fewer fills, 2.2× fewer insts** |
| **`TERRAIN_MACRO`** | **7,448** | 7 | 772,578 | **0:0** | **zero spills** |
| **`TERRAIN_MACRO_FAR`** | **10,872** | 7 | 773,557 | **0:0** | **zero spills** |
| **`WATER` (Stage 8 + sprites)** | 12,609 | 3 | **66,808** | 16:53 | dry pixels discarded immediately on alpha `> 0.75` |

**Recorded Real-GPU Headless Chrome Benchmark (`node scripts/bench-nd-browser.mjs --frames 30`, Intel ADL GT2, 1280×656, `EXT_disjoint_timer_query_webgl2`, 0 page errors):**

| Phase | FPS | `avg_ms` | `p50_ms` | `p95_ms` | `max_ms` | GPU Sum | Key GPU Passes (`avg / max ms`) |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| **`stationary_1x`** | **19.7** | **50.75** | **40.78** | 103.59 | **104.49** | **47.49 ms** | `TERRAIN 24.86/27.86`, `WATER 9.71/11.28`, `GRASS 4.29`, `MESH3D_ALPHA 4.03`, `TREE_SHADOW 2.53`, `POST 0.89` |
| **`panning_1x`** | **19.7** | **50.69** | **35.03** | 118.97 | **122.27** | **47.29 ms** | `TERRAIN 24.27/26.40`, `WATER 11.10/13.16`, `GRASS 3.94`, `MESH3D_ALPHA 3.36`, `TREE_SHADOW 2.50`, `POST 0.89` |
| **`zoom_sweep`** | **17.6** | **56.94** | **57.00** | 114.72 | **182.66** | **61.69 ms** | `TERRAIN 23.79/47.14`, `WATER 10.28/13.12`, `MESH3D_ALPHA 10.22`, `CLOUDS 5.55/9.89`, `TREE_SHADOW 4.75`, `GRASS 4.48` |
| **`wide_zoom_steady`** | **24.4** | **40.97** | **40.67** | 80.43 | **82.55** | **35.51 ms** | `TERRAIN 26.68/28.41`, `CLOUDS 7.76/8.23` (`CLOUDS/TERRAIN = 0.291`), `POST 0.46`, `SKY 0.40` |

*Pre-warm impact (`Step 5.2` without pre-warm vs `Step 5.3` with `gl_prewarm_pipelines()` + `glTexSubImage2D` chunk reuse): `stationary_1x max_ms` dropped **1,406.15 → 104.49 ms** (**13.5×**), `zoom_sweep max_ms` dropped **4,507.15 → 182.66 ms** (**24.7×**), and `zoom_sweep TERRAIN max` dropped **277.04 → 47.14 ms** (**5.9×**).*

**Status: Complete** (`21/21` GLSL ES 3.00 shader programs verified via `glslangValidator -l` in `test_seven_render.c`, `test_seven_gl_ffi.c` green with `blits=3.0(d=2.0,c=2.0)` / `texImg2D=0` / `bufData=0`, `wasm-audit: PASS`, zero JS changes).

---

## Phase 6 — Hardware-Filtered Shading, Fast Boot & SIMD Streaming (Wave 6)

**Problem (verified on real Intel ADL GT2 / ANGLE WebGL2 after Phase 5):**
Even with Phase 5 eliminating most Gen12 register spills and mid-zoom compilation stalls, real-GPU profiling (`1280×656`, `EXT_disjoint_timer_query_webgl2`) shows **72–97% of GPU frame time** is still spent on software-filtered integer `texelFetch` loops, per-pixel procedural gradient/4D noise ALU, dry-pixel water `discard`s, and 4× MSAA `RGBA16F`+`DEPTH32F_STENCIL8` FBO bandwidth — while cold boot still blocks **~14.1 s** compiling all 22 programs up front and zoom bursts spike CPU to **82.1 ms** (`ar_pop = 93,324`):

1. **Per-pixel software filtering & river loops (`TERRAIN` 24.8–26.7 ms = 52–75% of GPU frame)**:
   - `uHeightTex` is `GL_R16I` (`GL_NEAREST`), preventing hardware bilinear filtering. `sampleHeightSmooth()` (`mods/nd/seven/nd_seven_gl_shaders.c:1219-1293`) performs **16–32 integer `texelFetch`** + Catmull-Rom weights + 16-branch Laplacian per pixel inside `uTerrFastDist`, and unconditionally in `s_parts_fs_water` (`L2742-2784`) and `s_parts_vs_water` (`L2588-2621`).
   - `eval_river_surface()` (`L1475-1530`, duplicated in `s_parts_fs_water:2838-2878`) runs a **4×4 loop (up to 32 `texelFetch`)** per pixel on every wet/non-dry chunk regardless of whether the chunk contains river vertices.
2. **Per-pixel procedural noise ALU explosion (`TERRAIN_MACRO` 772k cycles at 0 spills; `WATER` 12,609 insts / 16:53 spills; `CLOUDS` 7.76 ms)**:
   - `s_parts_fs_terrain` runs **10–18+ `chaff_grad_noise2d`** calls per pixel (zoom micro-detail, grove ecology, meadow bed, rock strata, humidity, silt, valley fog, plus 6 macro octaves + `nd_gpu_eval_canonical_field` at wide zoom), and recomputes the exact same **9–12 biome `smoothstep` weights 3×** (`nd_eval_tex_bio_humidity`, Stage 4, Stage 6).
   - `s_parts_fs_water` runs **15+ procedural noise evaluations** (wind, micro-relief, 3-octave shore swash, 6-octave zoom waves, 2 capillary facets, 4× 3D caustic noises = 32 hashes, silt, ice), recomputing octaves `0..2` twice across `eval_shore_wave_swell_core` and `eval_zoom_water_waves_core`, and still links full analytic synoptic cloud fallback even though `WATER` only runs at `cur_dist <= 2000.0f` (CSM range).
   - `s_parts_fs_cloud` (`L5204`) evaluates **`chaff_grad_noise4d`** inside its 2–4 step raymarch loop — hashing **16 4D hypercube corners (~180 ALU ops)** per step.
3. **Disabled Early-Z / Early-Stencil & redundant shading**:
   - `WATER` renders full coastal chunk quads and relies on FS `discard` to skip dry pixels, disabling Early-Z and forcing `sampleHeightSmooth` + `texture(uSceneColor)` on dry land inside coastal chunks.
   - `nd_seven_gl_begin_frame_ex` (`mods/nd/seven/nd_seven_gl.c:1511`) disables `GL_CULL_FACE` across the entire terrain pass (and closed solid meshes).
   - `TREE_SHADOW` Pass A (`uShadowPass == 3`, `2.5–4.75 ms`) binds the full monolithic `mesh3d_pipe` shader with lighting/fog/cutaway/substrate compiled in; `s_m3d_fs_body_mid` (`L4272-4279`) evaluates 5 `chaff_grad_noise2d` + 19-branch biome switch per pixel.
4. **Cold-boot shader compilation stall (~14.1 s on WebGL2/ANGLE)**:
   - `init_pipelines()` (`mods/nd/seven/nd_seven_gl.c:710-736`) compiles **13 duplicate vertex shaders** (`vs_terr` 7×, `vs_cloud` 3×, `vs_mesh3d` 2×, `vs_csm` 5×) and `check_pipelines_ready()` (`L778-800`) blocks frame 0 until all **22** programs link — even though the initial 1× diorama frame only needs **~9** programs.
5. **FBO bandwidth, CPU zoom bursts & draw-call count**:
   - `gl_alloc_scene_targets()` (`mods/nd/seven/nd_seven_gl_occ.c:134-140`) uses **4× MSAA `RGBA16F` + `DEPTH32F_STENCIL8`** (**64 B/px**) even though `post_pipe` already provides FXAA and `nd_seven_gl_set_render_scales()` already supports zero-realloc sub-rect rendering.
   - `seven_dispatch_macro_backdrop()` (`mods/nd/seven/nd_seven.c:914`) runs unbounded synchronous `libareole` chunk populates on zoom jumps (`82.11 ms` CPU); chunk upload runs a CPU mipmap box-downsample loop (`nd_seven_gl.c:2036-2082`) and per-frame `torus_frame_double` (`L2093`). Wide zoom issues **159 chunk draws + ~318 texture binds**.

---

### Sub-Phase 6A — Fast Boot, Early-Stencil Water & Cheap Shadows (Quick Wins: 1–2d, Low Risk)

**Data Layout & Invariants:**
- **Two-Tier Pipeline Readiness State (`s_gl.boot_ready` vs `s_gl.pipelines_ready`)**:
  - **Shared VS handles**: Compile 4 shared vertex shader objects once (`vs_terr_sh`, `vs_cloud_sh`, `vs_mesh3d_sh`, `vs_csm_sh`) and attach across their respective variants before `glDeleteShader` post-link (cuts 22 VS compiles → 9).
  - **Tier A (Boot-Critical, 9 programs)**: `sky`, `TERRAIN_DRY_NEAR`, `TERRAIN_SEABED_NEAR`, `water`, `billboard`, `mesh3d`, `mesh3d_opaque`, `grass`, `csm`, `post`. First paint unblocks as soon as Tier A completes; `gl_prewarm_pipelines()` pre-warms Tier A on frame 0 and incrementally pre-warms each Tier B program on the first frame its `GL_COMPLETION_STATUS_KHR` flips true (max 1–2 pre-warms/frame).
  - **Tier B (Background Async)**: `TERRAIN_DRY_CSM`, `TERRAIN_SEABED_MID`, `TERRAIN_MACRO`, `TERRAIN_MACRO_FAR`, `TERRAIN_FULL`, `CLOUD_FULL`, `CLOUD_WIDE`, `CLOUD_WIDE_CSM`, `cloud_comp`, `ssao`, `bloom`, `terrain_shadow`. While a Tier B variant is still compiling, fallback cleanly to the ready Tier A variant (or skip optional `ssao`/`bloom` until ready).
- **Hardware Early-Stencil Bitmask (`GL_STENCIL_INDEX8`)**:
  - Bit `0x01`: Terrain written (existing/available).
  - Bit `0x08`: **Wet-candidate mask** written during `TERRAIN_SEABED_NEAR` / `TERRAIN_SEABED_MID` for chunks/fragments with `height < sea_level + 2.5` or `river_prox > 0`. `WATER` binds `glStencilFunc(GL_EQUAL, 0x08, 0x08)`, `glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP)` so GPU Early-Stencil/Hi-Z rejects 100% of dry land pixels before launching `s_parts_fs_water`.
- **Dedicated Depth-Only `mesh3d_shadow_pipe` (2 tiny variants: alpha-cutout + opaque)**:
  - Vertex shader outputs only `gl_Position` + `vUV` (for alpha cards) and `vKind`. Fragment shader does 1 atlas alpha fetch + `if (a < cutoff) discard;` (and empty FS for opaque trunks/rocks — zero `discard`, full Early-Z). Strips `uShadowPass == 3` completely from `mesh3d_pipe`.
- **Per-Instance CPU-Cached `subCol` (`gl_mesh3d_inst_t`, 128 B) & `GL_CULL_FACE`**:
  - Replace per-pixel/per-vertex `nd_eval_grove_ecology_weight` + `nd_eval_ground_substrate` (5× `chaff_grad_noise2d` + 19-branch biome switch) with a CPU-evaluated 1024-entry direct-mapped cache packed into `gl_mesh3d_inst_t.sub_col[4]` (`layout(location = 10) in vec4 a_isub`, `flat out/in vec3 vSubCol`), keeping zero substrate noise in both `vs_mesh3d` and `fs_mesh3d`.
  - Verify CCW winding on `init_grid_buffers()` terrain grids + skirts and closed `mesh3d_opaque` trunks/rocks; enable `glEnable(GL_CULL_FACE)` (`GL_BACK`) for terrain and water passes.

**Deliverables (6A):**
1. Shared VS compilation + Tier A (13 boot-critical) / Tier B (9 active background) async pipeline state machine in `mods/nd/seven/nd_seven_gl.c` (`init_pipelines`, `check_pipelines_ready`, `init_tier_b_pipelines`, `gl_prewarm_pipelines`) — deferring Tier B queuing until after Frame 0 paints and never eagerly compiling/prewarming the 112-spill `TERRAIN_FULL` / 81-spill `CLOUD_FULL` emergency fallbacks; occluded-water chunks shade via `SEABED_NEAR`/`SEABED_MID` (split-water range bridged cleanly to `5500 wu`) and mid-zoom clouds with `<= 2` clustered lights dispatch `CLOUD_WIDE(_CSM)`.
2. Early-Stencil `0x08`/`0x01` water pass gating + `ND_WATER_CSM_ONLY` stripping of synoptic cloud fallback + shared octaves `0..2` between `eval_shore_wave_swell_core` and `eval_zoom_water_waves_core` + narrow coastal-band gating of pre-discard `eval_water_waves_lo()` in `mods/nd/seven/nd_seven_gl_shaders.c` & `nd_seven_gl.c`.
3. Dedicated `mesh3d_shadow_alpha` / `mesh3d_shadow_opaque` depth-only pipelines, CPU-cached per-instance `a_isub` (`gl_mesh3d_inst_t`) in `mesh3d`, and `GL_CULL_FACE` for terrain + water passes in `mods/nd/seven/nd_seven_gl.c` & `nd_seven_gl_billboard.c`.

**Status (6A): Complete** — Audited and verified on Intel ADL GT2 (Mesa OpenGL ES 3.2 / ANGLE WebGL2, `1280×656`, `?stage=lamina-seven`, `EXT_disjoint_timer_query_webgl2`), `23/23` GLSL ES 3.00 programs validated via `glslangValidator -l`, `0` console/WebGL errors:

| Metric / Phase (`1280×656`) | Phase 5 Baseline | Phase 6A Measured (clock-matched) | Notes |
| :--- | ---: | ---: | :--- |
| **Cold-boot first paint (empty `MESA_SHADER_CACHE_DIR` + fresh profile)** | ~14.1–16.2 s | **9.70 s** (`tWorker=405 ms`) | **-40% (-6.48 s)** via shared VS + Tier B deferred past Frame 0 + skipping eager `TERRAIN_FULL`/`CLOUD_FULL` compiles; remaining ~9.3 s is Tier A `WATER`/`SEABED_NEAR` ALU JIT (targeted by 6B) |
| **`stationary_1x`** | `19.7 fps`, `GPU 47.49 ms` (`WATER 9.71`, `TREE_SHADOW 2.53`, `GRASS 4.29`, `MESH3D_ALPHA 4.03`) | `p50 60.43 ms`, `GPU 50.03 ms` (`TERRAIN 26.15`, `MESH3D_ALPHA 15.63`, `GRASS 3.09`, **`TREE_SHADOW 1.79`**, **`WATER 1.41`**) | Dense-forest spawn vs Phase 5 beach spawn: **`WATER` -85.5%**, **`TREE_SHADOW` -29.2%**, **`GRASS` -28.0%**, `MESH3D_ALPHA` -21.7% (`19.97→15.63 ms`) via CPU-cached `a_isub`; `max_ms` `141 ms` warm (`1044 ms` 1-frame Tier B prewarm on fresh profile) |
| **`panning_1x`** | `19.7 fps`, `max 122.27 ms`, `WATER 11.10`, `TREE_SHADOW 2.50` | `14.7 fps`, `p50 54.86 ms`, `max 142.07 ms`, `GPU 58.35 ms` (**`WATER 3.50`**, **`TREE_SHADOW 1.98`**) | **`WATER` -68.5%**, **`TREE_SHADOW` -20.8%**; turbo-clock run hits `26.2 fps` (`GPU 31.35 ms`, `max 79.16 ms`) |
| **`zoom_sweep`** | `17.6 fps`, `max 182.66 ms`, `WATER 10.28`, `TREE_SHADOW 4.75` | `p50 50.44 ms`, **`max 482.5 ms`** (turbo: `26.8 fps`, `max 105.46 ms`), **`WATER 3.38`**, **`TREE_SHADOW 3.33`** | Eliminated `5,487 ms` `TERRAIN_FULL`/`CLOUD_FULL` mid-zoom stall (**-11.4×**); **`WATER` -67.1%**, **`TREE_SHADOW` -29.9%** |
| **`wide_zoom_steady`** | `24.4 fps`, `avg 40.97 ms`, `GPU 35.51 ms` (`TERRAIN 26.68`, `CLOUDS 7.76`) | **`24.8 fps`**, `avg 40.40 ms`, `GPU 36.21 ms` (`TERRAIN 26.67`, `CLOUDS 8.43`) | Clock-matched parity (turbo-clock run hits `38.7 fps`, `GPU 22.16 ms`) |
| **WebGL FFI (`run-test-seven-gl-ffi.sh`, High stationary)** | `717.0` FFI/f | **`520.0` FFI/f** (`getUnifLoc=0`, `texImg2D=0`, `bufData=0`, `cmdFlushes=5.0`) | Zero steady-state allocations; `436.0` FFI/f wide-zoom (`2.7` calls/chunk) |
| **CPU perf (`run-test-seven-perf.sh`, High stationary)** | `1.2–1.3 ms` | **`1.53 ms`** (`scen 0.35 ms`, cached `a_isub`) | Bounded 1024-entry substrate cache; zero mallocs |
| **GLSL & ABI gates** | `21/21` GLSL | **`23/23` GLSL ES 3.00**, `make test-graphics: PASS`, `wasm-audit: PASS`, `0` console/WebGL errors | Zero JS changes; pure-C boundaries verified |

---

### Sub-Phase 6B — Pre-Baked Chunk Derived Maps & Tileable Noise LUTs (Core GPU Breakthrough: 2–3d, Med Risk)

**Data Layout & Invariants:**
- **Chunk Derived Map (`gl_chunk_tex_t.derived_tex`: `35×35` `GL_RGBA8` or `GL_RGBA16F`, `GL_LINEAR`) + Float Heightmap (`GL_R16F` or `GL_R32F`, `GL_LINEAR`)**:
  - Baked **once on CPU upload** (`nd_seven_gl_flush_terrain_chunk`) when `slot->rev != rev` (~1,225 texels × ~20 ns = ~25 µs/chunk):
    - `R`: Analytic Catmull-Rom slope `dh/dx` (octahedral or scaled `[-1, 1]` with chunk-span normalization).
    - `G`: Analytic Catmull-Rom slope `dh/dy`.
    - `B`: Pre-computed Laplacian curvature `lap` (replaces 16-branch neighbor loop in `sampleHeightSmooth`).
    - `A`: Pre-computed river water table offset / proximity `river_z_rel` (replaces the entire 4×4 `eval_river_surface` `texelFetch` loop in BOTH `TERRAIN` and `WATER`).
  - `gl_chunk_tex_t.has_river` (`uint8_t`): Computed during upload scan; passed via uniform flag (or zero-alpha fast-path) so dry/ocean-only chunks skip river shading entirely.
  - **Shoreline Invariance Contract (`docs/SHORE.md`)**: CPU `nd_field_sample` remains the single source of truth for vertex heights and gameplay/physics collision; the float `height_tex` stores exact Q12.4 heights (`val * 0.0625f` — exact in `R32F`, or exact to `0.0625` in `R16F` up to `±1024 wu` with chunk-base offset `uChunkBaseZ` for mountains!). Using `float h_rel = val * 0.0625f - chunk_base_z` guarantees **exact 0.0625 wu precision** even in `GL_R16F` across the entire `[-2150, +2000]` elevation range at half the memory of `R32F`!
- **Tileable Procedural Noise + Gradient LUT Textures (baked once at startup from `chaff_grad_noise2d` / `chaff_grad_noise3d`)**:
  - `s_gl.noise2d_tex` (`256×256` `GL_RGBA8` or `GL_RGBA16F`, `GL_REPEAT`, `GL_LINEAR_MIPMAP_LINEAR`): `R = val`, `G = dVal/dx`, `B = dVal/dy`, `A = second uncorrelated octave val`. One hardware bilinear `texture()` replaces ~60 ALU ops + 4 corner integer hashes per `chaff_grad_noise2d` octave across `TERRAIN` detail/ecology/strata/macro, `WATER` waves/capillaries/silt/ice, and `MESH3D`.
  - `s_gl.noise3d_tex` (`64×64×64` `GL_TEXTURE_3D` `GL_RGBA8`, `GL_REPEAT`, `GL_LINEAR`, 1 MB VRAM): `R = billow`, `GBA = 3D analytic gradient`. Replaces `chaff_grad_noise4d` (16 hypercube corner hashes per raymarch step) in `s_parts_fs_cloud` (scrolling `(u, v, z + wBoil)` or blending 2 phase-shifted trilinear taps) AND replaces 4× 3D caustic noises (32 hashes) in `nd_caustic_web()`.
  - **Parity Guardrail**: Keep deterministic analytic `chaff` ONLY for CPU-coupled shoreline swash/tide phase (`eval_astronomical_tide`, focal water-entry probe) where exact CPU↔GPU sync matters; use LUT textures for all pure visual surface detail, macro textures, ripples, caustics, and cloud billow sculpting.
- **Single-Pass Biome Weight Deduplication + VS Low-Freq Hoisting**:
  - Compute the 9–12 biome `smoothstep` weights **once** in `s_parts_fs_terrain` into a `struct NdBiomeW` and reuse across humidity, Stage 4, and Stage 6 (eliminating 2 redundant full evaluations).
  - Hoist low-frequency static fields (`groveMacro` 520 wu wavelength, `meadowBed` 176 wu, `worldClimate` 22,000 wu) into `s_parts_vs_terrain` varyings.

**Deliverables (6B):**
1. `gl_chunk_tex_t` `R16F` (chunk-base-offset) heightmap + `RGBA8` derived `(nx, ny, lap, river)` map baked on upload; replace `sampleHeightSmooth` and `eval_river_surface` 16–32 `texelFetch` loops in `TERRAIN` and `WATER`.
2. Startup bake & upload of `noise2d_tex` (256×256) and `noise3d_tex` (64³ `GL_TEXTURE_3D`), plus `nd_noise2d_lut()` / `nd_noise3d_lut()` GLSL helpers replacing hot-path `chaff_grad_noise2d` / `chaff_grad_noise4d` in `TERRAIN`, `WATER`, and `CLOUDS`.
3. Biome weight deduplication (`struct NdBiomeW`) and VS hoisting of low-frequency ecology/climate fields in `s_parts_vs_terrain` / `s_parts_fs_terrain`.

---

### Sub-Phase 6C — Cloud Reprojection, FBO Bandwidth & Adaptive Scaling (Polish & Scaling: 1–2d, Low–Med Risk)

**Data Layout & Invariants:**
- **Half-Res / 2×2 Temporal Checkerboard Clouds (`cloud_tex_prev`, `cloud_tex_curr`)**:
  - `gl_cloud_alloc_dims()` (`nd_seven_gl_occ.c:62`): Default High clouds to `0.50×–0.67×` paired with the **existing depth-aware bilateral upsampler** in `cloud_comp_pipe` (`nd_seven_gl_clouds.c:720`), OR enable 2×2 interleaved temporal reprojection (`gl_FragCoord` sub-pixel phase `frame_idx & 3`, reprojecting prior cloud pixels via `prev_vp * inv_curr_vp` with depth/disocclusion clamp).
  - Hoist per-body uniforms (`uPlanetCenter`, `uBodyRot`, `uTorusRadii`) outside the per-chunk loop in `nd_seven_gl_flush_clouds` (`L670-680`).
- **FBO Bandwidth Reduction (`gl_alloc_scene_targets`, `nd_seven_gl_occ.c:134-140`)**:
  - **Configurable MSAA (`0× + FXAA`, `2×`, `4×`)**: Default integrated-GPU/Medium/Low profiles to `0×` (relying on existing `post_pipe` FXAA) or `2×` MSAA, keeping `4×` as an explicit High/Ultra option — cuts ROP + resolve bandwidth **2×–4×** and eliminates `glBlitFramebuffer` MSAA resolve cost entirely at `0×`.
  - **HDR `GL_R11F_G11F_B10F` (4 B/px vs 8 B/px `RGBA16F`)**: Once the 3-state seabed/sprite alpha contract moves to **2 stencil bits** (`0x08` seabed, `0x10` submerged sprite), scene HDR color no longer needs an alpha channel, halving color attachment memory & resolve bandwidth!
  - **Dynamic `far_z` & `GL_DEPTH24_STENCIL8` option**: Tighten near-field `far_z` from fixed `80,000,000` at `d = 300` to dynamic horizon distance `max(d * 60.0f, 45000.0f)` (expanding smoothly to `8e7` above `d > 2000`), dramatically improving near/mid depth precision and enabling `GL_DEPTH24_STENCIL8` (4 B/px vs 8 B/px `DEPTH32F_STENCIL8`) when in surface/mid regimes.
- **Closed-Loop Dynamic Resolution Scaling (DRS)**:
  - Feed rolling GPU frame time (or `rAF` frame delta when timer queries unavailable) into a damped hysteresis controller driving existing `nd_seven_gl_set_render_scales(eff_res_scale, eff_cloud_scale)` (`nd_seven_gl.c:1216`) between `0.75–1.00` during active camera motion/zoom, snapping smoothly back to `1.00` within ~200 ms once settled. Zero FBO reallocation.

**Deliverables (6C):**
1. Half-res / temporal checkerboard cloud mode + hoisted body uniforms in `nd_seven_gl_clouds.c` & `nd_seven_gl_occ.c`.
2. Stencil-backed water/sprite state contract allowing `GL_R11F_G11F_B10F` scene color + selectable `0×/2×/4×` MSAA + dynamic horizon `far_z` in `nd_seven_gl_occ.c` & `nd_seven_gl.c`.
3. Closed-loop DRS controller wired into `nd_seven_eval_alt_profile()` (opt-in / Auto quality preset).

---

### Sub-Phase 6D — 4-Wide SIMD Chunk Populate & `sampler2DArray` Instanced Terrain (CPU & Draw-Call Collapse: 2d, Med Risk)

**Data Layout & Invariants:**
- **4-Wide SIMD Batch Terrain Populate (`external/libareole` + `external/nd-world`)**:
  - New batched callback `ar_eval_row4_fn(const int32_t x[4], int32_t y, float out_h[4], uint32_t out_rgba[4], void *ctx)` implemented using `<wasm_simd128.h>` (`v128_t`) on WASM (`-msimd128` already enabled in P1) and `<emmintrin.h>` (SSE2) on x86-64.
  - Vectorizes `chaff_grad_noise2d` + octave accumulation across 4 adjacent grid columns simultaneously, guaranteeing **bit-exact** IEEE-754 parity with scalar `nd_field_sample` (verified by standalone unit test diffing 4-wide vs scalar over 10,000 random world coordinates).
  - Replace CPU mipmap box-downsample loop in `nd_seven_gl_flush_terrain_chunk` (`L2036-2082`) with `glGenerateMipmap(GL_TEXTURE_2D)` and cache per-chunk torus center `cpos[3]` on `gl_chunk_tex_t` (eliminating per-frame `torus_frame_double` at `L2093`).
- **`GL_TEXTURE_2D_ARRAY` Terrain Pool + Instanced Chunk Draw + VAOs**:
  - Allocate fixed `GL_TEXTURE_2D_ARRAY` pools (`256` layers × `35×35` each): `height_array` (`R16F`, ~627 KB), `derived_array` (`RGBA8`, ~1.25 MB), `substrate_array` (`RGBA8` mipmapped, ~1.7 MB) — total **< 3.6 MB VRAM**.
  - `gl_chunk_inst_t` (48 B/chunk, 3× `vec4` instance attributes): `(ox, oy, span, base_z)`, `(rel_ox, rel_oy, sea_level, flags_and_layer)`, `(p_center_or_misc)` uploaded once per frame via a single `glBufferSubData`.
  - Group visible chunks by `(nd_terr_var_t, grid_lod)` and issue **1 `glDrawElementsInstanced` per group** → collapses **159 wide-zoom chunk draws + 318 texture binds** to **~3–5 draws + 3 binds**.
  - Expose WebGL2 **VAOs (`glCreateVertexArray` / `glBindVertexArray` / `glDeleteVertexArray`)** in `external/liblamina/htdocs/webgl-cap.js` + `lam_gl` to eliminate per-bucket 7-attribute `glVertexAttribPointer` / `glVertexAttribDivisor` churn in `gl_draw_mesh3d_bucket` and per-pass state setup.

**Deliverables (6D):**
1. Standalone 4-wide SIMD `chaff_grad_noise2d_4x` + `nd_field_sample_4x` with bit-exact scalar equivalence unit test in `external/libchaff` / `external/nd-world` / `external/libareole`.
2. `glGenerateMipmap` + cached `gl_chunk_tex_t.cpos[3]` in `mods/nd/seven/nd_seven_gl.c`.
3. VAO support in `external/liblamina` (`webgl-cap.js` + native stub + bridge test) and `GL_TEXTURE_2D_ARRAY` instanced terrain batching in `mods/nd/seven/`.

---

**Phase 6 Target Exit Criteria (Intel ADL GT2 `1280×656`, `scripts/bench-nd-browser.mjs --frames 30`):**

| Metric | Phase 5 Baseline | Phase 6 Target | Primary Driver |
| :--- | ---: | ---: | :--- |
| **`stationary_1x` FPS / GPU ms** | 19.7 FPS / 47.49 ms | **45–60 FPS / 15–22 ms** | 6B derived maps + noise LUT, 6A Early-Stencil water, 6C FBO/MSAA |
| **`wide_zoom_steady` FPS / GPU ms** | 24.4 FPS / 35.51 ms | **50–60 FPS / 12–18 ms** | 6B macro noise LUT + 6C half-res/temporal clouds + 6D instanced chunks |
| **`TERRAIN` pass (`1x` / `wide`)** | 24.86 / 26.68 ms | **8–12 / 7–10 ms** | 6B `R16F`+derived map + 2D noise LUT + biome dedup + `GL_CULL_FACE` |
| **`WATER` pass (`1x`)** | 9.71–11.10 ms | **3–4 ms** | 6A Early-Stencil + `CSM_ONLY` + shared octaves + 6B noise/caustic LUT |
| **`CLOUDS` pass (`wide`)** | 7.76 ms | **2–3 ms** | 6B 3D volume noise LUT + 6C half-res/checkerboard reprojection |
| **`TREE_SHADOW` + `MESH3D_ALPHA`** | 6.56–14.97 ms | **2.5–5 ms** | 6A depth-only `mesh3d_shadow` + VS `subCol` + static VBO skip |
| **Cold-boot first paint (WebGL2/ANGLE)** | ~14.1 s | **~2.5–3.5 s** | 6A shared VS handles + Tier A/B lazy pipeline compile |
| **`INTEL_DEBUG=fs` spills:fills** | `SEABED_MID` 29:47, `WATER` 16:53 | **0:0 across all active variants** | 6B elimination of 16–32 `texelFetch` & procedural noise chains |
| **Zoom CPU burst (`test_seven_perf`)** | 82.11 ms (`ar_pop 93k`) | **≤ 25 ms** | 6D 4-wide SIMD `libareole` chunk populate |
| **Wide-zoom terrain draws / FFI** | 159 draws / 537 FFI | **≤ 8 draws / ≤ 120 FFI** | 6D `GL_TEXTURE_2D_ARRAY` instanced terrain + VAOs |
| **Visual & Contract Gates** | `21/21` GLSL, 0 errors | **All GLSL ES 3.00 pass**, `docs/SHORE.md` parity, 0 console/WebGL errors, `make test-graphics` & `make test-fast` exit `0` | Per-sub-phase golden-frame & unit verification |

**Status: In Progress (Sub-Phase 6A Complete; Sub-Phases 6B–6D Planned)**

---

## Recommended Execution Sequence

```text
1. P0 (Profiler & Baseline)          → verify: baseline ms / FFI / draw-call table recorded
2. P1 (Bridge, Leak, Hoist, -O3)     → verify: 0 uniform leak, ≥50% FFI drop, Tier-0 + test-graphics green
3. P2 (Flora Cache, Probes, Areole)  → verify: ≥60% CPU drop stationary, Tier-0 + test-graphics green
4. P3 (Sky Early-Z, Shaders, Grass)  → verify: ≥40% GPU/high-res drop, Tier-0 + test-graphics green
5. P4 (UBO/Ring, RAM, Server/DB)     → verify: WASM/RAM/server targets met, make test-fast green
6. P5 (Zero-Spill iGPU & Prewarm)    → verify: INTEL_DEBUG=fs spills slashed, 21/21 GLSL pass, hitch-free real-GPU bench
7. P6A (Fast Boot, Stencil Water, Shadow) → verify: cold boot ~3s, WATER -4–6ms, TREE_SHADOW -50%, all GLSL pass
8. P6B (Chunk Derived Map & Noise LUTs)   → verify: 0:0 spills all variants, TERRAIN/WATER/CLOUDS 2–3× faster, shore parity
9. P6C (Cloud Reproj, FBO/MSAA, DRS)      → verify: R11F_G11F_B10F + stencil contract, 45–60 FPS on Intel ADL GT2
10. P6D (SIMD Populate & Array Instancing) → verify: bit-exact SIMD test, zoom CPU ≤25ms, wide-zoom draws 159→≤8
```
