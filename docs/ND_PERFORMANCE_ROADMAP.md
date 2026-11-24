# NeverDark Performance & Efficiency Roadmap

Phased, measurement-driven roadmap to make NeverDark (`mods/nd`, `libareole`, `liblamina`, `axil-nd`, `nd-world`, `nd-view`) dramatically faster on both integrated GPUs and modest CPUs — while preserving pure-C isomorphic UX, zero site-specific JS, visual fidelity, and existing unit/integration contracts.

Cross-references:
- Build, WASM size, `libbud` arena, and Seven modularization: `docs/CODEBASE_IMPROVEMENT_ROADMAP.md`
- Terrain/LOD architecture: `docs/TERRAIN_ROADMAP.md`, `docs/ANALYSIS_TERRAIN_CHUNKS_LOD_STRATOSPHERE.md`
- Water & cloud specifications: `docs/WATER_REQUIREMENTS.md`, `docs/VOLUMETRIC_CLOUDS_AND_4D_NOISE.md`

---

## Non-Negotiable Guardrails

1. **Measure before & after every phase.** Every phase records real frame-time (ms), draw-call, FFI-call, and allocation deltas on the same camera preset and quality level. No unproven "should be faster" changes.
2. **Authoritative asset paths.** JS bridge edits happen ONLY in `external/liblamina/htdocs/{webgl-cap.js,lamina-cap.js}` and `external/libstele/htdocs/stele-client.js`, staged into `htdocs/` via `make stage-assets` and verified with `sh scripts/check-asset-sync.sh` and `sh scripts/check-no-site-specific-js.sh`.
3. **Pure-C isomorphic UX boundary.** `mods/nd/ux/*.c` keeps zero `XY_`/`corm_`/`source_`/`axil_` references and passes `sh scripts/check-module-boundaries.sh` and `sh scripts/check-wasm-imports.sh`.
4. **No visual regressions at equal settings.** Optimizations that trade visual quality (e.g., bloom toggle, bilinear vs. bicubic terrain sampling, tuft LOD step-down) must either be mathematically imperceptible at distance or explicitly gated by existing quality presets.
5. **Abstraction-first TDD.** Changes in `external/libareole`, `external/liblamina`, and `external/nd-*` land with standalone unit tests before integrating into `mods/nd/ux/`.

---

## Overview

| Phase | Theme | Effort | Risk | Status | Headline Impact |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **P0** | Frame-Phase Profiler & Baseline Harness | Small (2–3h) | Low | Planned | Per-phase CPU/FFI/draw-call counters to prove every subsequent win |
| **P1** | Zero-Risk Hot-Path & Bridge Quick Wins | Small–Med (½–1d) | Low | Planned | Eliminates 2k–6k DOM queries/frame, 10.8k/min uniform leak, 4k–8k redundant FFI calls, 5× map re-ingest; enables `-O3` + `-msimd128` |
| **P2** | CPU Frame-Time Killers (Data Layout & Caching) | Medium (1–2d) | Low–Med | Planned | Caches 10.5k-cell botanical lattices, removes 16k–40k `ar_probe_view`/frame, cuts `libareole` occluder/refinement overhead, fixes mesh sort + billboard flush churn |
| **P3** | GPU Shader, Fill-Rate & Pass Trimming | Medium (1–2d) | Med | Planned | Sky-after-terrain Early-Z, pre-linearized GLSL constants (~135 `pow`/px saved), single-eval wind, opaque vs. alpha-tested `mesh3d`, instanced grass tufts, trimmed blits/bloom |
| **P4** | Structural Architecture, Memory & Server/Protocol | Large (2–3d) | Med | Planned | WebGL command ring buffer + UBOs, GLSL build stripping, `libareole` single vertex buffer (-20+ MB RAM), non-destructive BCP updates, `axil-nd`/`nd-view`/`nd-world` DB/RAM fixes |

---

## Phase 0 — Frame-Phase Profiler & Reproducible Baseline

**Problem:** Without per-phase timing and call counters, optimizations risk chasing noise or trading CPU time for hidden JS/GPU stalls.

**Data Layout & Invariant:**
- Compact POD counter struct `nd_perf_frame_t` (updated in-place each frame, zero heap allocation, compile/runtime toggleable via settings/debug overlay):
  - CPU phase timers (ms): `sync_ingest_ms`, `areole_query_ms`, `occluder_ms`, `terrain_flush_ms`, `botanical_emit_ms`, `billboard_mesh_ms`, `cloud_precull_ms`, `post_ms`.
  - Counters: `terrain_chunks`, `flora_probes`, `bb_flushes`, `mesh3d_count`, `ar_populates`.
- Optional JS bridge counter mode in `external/liblamina/htdocs/webgl-cap.js`: counts `glCalls`, `uniformUploads`, `texUploads`, `bufferUploads`, `uniformLocsLen` per second.

**Deliverables:**
1. Add lightweight per-phase timing/counter struct in `mods/nd/ux/` (disabled by default in hot path when overlay is hidden; zero overhead when off).
2. Add optional counter readout hook in `external/liblamina/htdocs/webgl-cap.js` to observe FFI call volume and verify `uniformLocs.length` stops growing.
3. Capture baseline numbers (Desktop + constrained/low-res preset, stationary + panning + zooming) in a short benchmark note before starting P1.

**Exit Criteria:**
- Baseline numbers recorded for stationary, panning, and zooming scenarios.
- `make -C mods/nd && make site-wasm && sh scripts/check-asset-sync.sh && sh scripts/check-no-site-specific-js.sh` exits `0`.

---

## Phase 1 — Zero-Risk Hot-Path, JS Bridge & Compiler Wins (Wave 1)

**Problem (verified in source):**
1. **`getGl()` DOM thrash** (`external/liblamina/htdocs/webgl-cap.js:458-460`): Every WebGL import (~2,000–6,000+ calls/frame) invokes `findCanvas()` (`querySelector('[data-bud-webgl]')` / Worker Map scan) before checking the cached `gl` handle.
2. **Per-frame `glGetUniformLocation` + unbounded memory leak** (`mods/nd/ux/nd_seven_gl_occ.c:354-377`, `external/liblamina/htdocs/webgl-cap.js:132,160,318-321,1074-1086`): `gl_build_hiz_pyramid()` queries `glGetProgramParameter` 2× and `glGetUniformLocation` 3× every frame. Each JS call allocates `new TextDecoder()` and appends to `uniformLocs`/`uniformMeta` (~10,800 leaked entries/min at 60 FPS).
3. **Per-chunk redundant terrain uniform/state setup** (`mods/nd/ux/nd_seven_gl.c:1316-1583`): For up to 160–192 chunks/frame, `nd_seven_gl_flush_terrain_chunk()` re-runs `torus_frame_double`, `glUseProgram`, 4 static texture binds, VBO/IBO/attrib binds, ~30 frame-constant uniforms (4,000–8,000 redundant FFI calls + 650–2,000 JS `Float32Array` allocations/frame), and an O(384) linear chunk texture cache scan (`nd_seven_gl.c:1348-1371`).
4. **Full CPU texture cloning & `preserveDrawingBuffer` copies**:
   - `gl_update_clustered_cloud_lighting()` (`mods/nd/ux/nd_seven_gl_clouds.c:443-448`) uses `glTexImage2D` instead of `glTexSubImage2D`, triggering full JS buffer clones (198 KB + 4 KB) in `webgl-cap.js`.
   - `external/liblamina/htdocs/webgl-cap.js:465` and `external/liblamina/htdocs/lamina-cap.js:167` set `preserveDrawingBuffer: true`, forcing a full-screen compositor copy every frame.
5. **Accidental CPU cache bypasses & 5×/frame map re-ingestion**:
   - `nd_seven_sync_origin()` (`mods/nd/ux/nd_seven_motion.c:114-141`) calls `known_tile_ingest_map(st)` before its unchanged-position early return, running 5×/frame (including on `fps_cap`-skipped frames in `mods/nd/ux/nd_ui.c:418-431`).
   - `s_block_bb_cache` (`mods/nd/ux/nd_seven.c:3989-4009`) requires `b_frustum_state == 2`, so every frustum-straddling 8×8 block (`== 1`) misses the cache and rebuilds per-room every frame.
   - `s_spatial_cache[3][3]` (`mods/nd/ux/nd_seven_objects.c:767-796`) is smaller than the `7×7` near-field loop, causing 40–41/49 tiles to miss every frame.
6. **Suboptimal compiler flags** (`build.mk:27-94` and native submodules): `nd.wasm` builds with `-Os` without `-O3`, `-msimd128`, `-flto`, `-fno-math-errno`, `-ffunction-sections -fdata-sections`, or `wasm-opt`; native `axil-nd`, `nd-world`, `liblamina`, and `dev` `mods/nd/nd.so` compile at `-O0`.

**Data Layout & Invariants:**
- `gl_hiz_prog_t`: cache `loc_tex`, `loc_ts`, `loc_uvs`, and `linked` flag once on program completion; never call `glGetUniformLocation` inside the render loop.
- Terrain chunk texture LRU: open-addressed hash table keyed by `(world, level, cx, cy)` indexing the 384-slot pool → O(1) lookup.
- `s_spatial_cache`: expand to `8×8` (power-of-two masked direct index `(rx & 7)`, `(ry & 7)`) so the entire `7×7` near-field fits with zero thrashing.
- `s_block_bb_cache`: cache full block contents whenever `b_frustum_state >= 1` (`1` or `2`), then cull individual cached entries against the frustum when `b_frustum_state == 1`.

**Deliverables:**
1. Fast-path `getGl()` in `external/liblamina/htdocs/webgl-cap.js`; reuse module-level `TextDecoder`/`TextEncoder` singletons across `webgl-cap.js` and `external/libstele/htdocs/stele-client.js`; audit and disable `preserveDrawingBuffer: true` where not needed for screenshot readback (or toggle only for headless capture).
2. Cache Hi-Z program link state & 3 uniform locations in `mods/nd/ux/nd_seven_gl_occ.c`; switch clustered cloud light texture updates in `mods/nd/ux/nd_seven_gl_clouds.c` to pre-allocated storage + `glTexSubImage2D`.
3. Split `nd_seven_gl_flush_terrain_chunk()` into `begin_terrain_pass()` (once per pass: program, shared textures, VBO/IBO/attribs, frame-constant uniforms) + per-chunk uniform delta upload + O(1) chunk cache lookup; avoid `Float32Array` allocations per uniform call in `webgl-cap.js` via subarray views/scratch buffers.
4. Fix `known_tile_ingest_map(st)` generation/dirty gating in `mods/nd/ux/nd_seven_motion.c` + skip on `fps_cap`-dropped frames in `mods/nd/ux/nd_ui.c`; enable `s_block_bb_cache` for `b_frustum_state == 1` in `mods/nd/ux/nd_seven.c`; expand `s_spatial_cache` to `8×8` in `mods/nd/ux/nd_seven_objects.c`.
5. Upgrade `nd.wasm` compile flags in `build.mk` to `-O3 -fno-math-errno -ffunction-sections -fdata-sections` (evaluate `-msimd128` and `-flto` / `wasm-opt` with fallback when unsupported) and ensure release/perf native builds use `-O2`/`-O3`.
6. Stage JS assets via `make stage-assets`.

**Exit Criteria:**
- `uniformLocs.length` stays constant over 5 minutes of runtime (0 leak).
- Per-frame WebGL FFI calls drop by **≥ 50%**; DOM queries in `getGl()` drop to **0/frame** steady-state.
- `make -C mods/nd && make site-wasm && make stage-assets` exits `0`.
- `sh scripts/check-asset-sync.sh && sh scripts/check-no-site-specific-js.sh && sh scripts/check-module-boundaries.sh && sh scripts/check-wasm-imports.sh` exits `0`.
- `sh tests/unit/run-test-lamina-bridge.sh && sh tests/unit/run-test-seven-render.sh && make test-mod MOD=nd` exits `0`.

---

## Phase 2 — CPU Frame-Time Killers: Data Layout & Caching (Wave 2)

**Problem (verified in source):**
1. **5 uncached procedural botanical & relief lattices** (`mods/nd/ux/nd_seven.c:2917-3778`): Re-evaluated every frame even when the camera is stationary — up to 10,565 lattice points/frame (~40k–75k `meadow_noise_2d`, ~25k–45k `sinf`/`cosf`, ~15k–25k height probes). Pass 3 runs cheap hash density rejections (`sh % 3` / `sh % 9`) *after* frustum, height, trig, and noise.
2. **8× `ar_probe_view` per nearby 3D mesh** (`mods/nd/ux/nd_seven_gl_billboard.c:1105-1120`, `mods/nd/ux/nd_seven_gl_internal.h:907-925`): Every grass/flora 3D mesh within 650 wu calls `shadow_corner_terrain_dz` 4× (2 `nd_seven_ground_elevation_f` each) = 8 `ar_probe_view` calls/mesh/frame (16,000–40,000 probes/frame), and `ar_probe_internal` interpolates full 4-channel float RGBA when only scalar height is needed.
3. **`libareole` software occluder & quadtree refinement overhead** (`external/libareole/src/areole.c:512-647,1829-2280`):
   - `ar_occluder_rasterize_node()` rescans all 2,401 immutable chunk heights per leaf (up to 501k reads/frame), runs double-precision `corolla_eval_frame` 23,200×/frame, and rasterizes 30,720 triangles/frame into a 96×54 buffer.
   - `ar_system_query_matrix()` runs an O(N²) linear scan re-hashing up to 4 Morton keys per candidate per iteration (~51,200 hash lookups/frame).
   - `mods/nd/ux/nd_seven.c:171-220` sets `max_populates_per_frame = 0` and `step_q16 = 0`, causing synchronous zoom spikes (62k–86k noise evals/chunk).
4. **Wasted Shell-sort & split billboard flushes** (`mods/nd/ux/nd_seven.c:1318,3829-3854`, `mods/nd/ux/nd_seven_gl_billboard.c:1205-1370`):
   - Shell-sorts up to 24,576 × 56 B structs (1.31 MB) recomputing projections in the inner loop — then immediately counting-sorts `mesh_id > 0` by `mesh_id`, throwing away the Shell sort.
   - `MID_STREAM_CHUNK = 2048` triggers repeated `nd_seven_gl_flush_billboards()` calls per frame, re-binding `tree_shadow_fbo`, reallocating 1.84 MB VBOs with `glBufferData`, and issuing 640–1,920 `glVertexAttribPointer` calls.
5. **Uncached CPU cloud-shell pre-cull** (`mods/nd/ux/nd_seven_gl_clouds.c:7-119`): Worst-case clear sky at `dist > 480` runs ~24,000 2D gradient noises (96,000 `sqrtf`) per frame on CPU.

**Data Layout & Invariants:**
- **Flora/Relief Block Cache (`nd_flora_block_t`)**: Fixed LRU pool (e.g. 128–256 world-space 8×8 blocks keyed by `(world, bx, by, biome_rev)`). Each cached entry stores precomputed `(wx, wy, wz, dz_e, dz_w, dz_s, dz_n, mesh_id, scale, yaw, tint)` so static lattice noise, trig, and ground-slope probes execute **once per block**, never per frame.
- **Separate Emit Queues**: Emit `mesh_id > 0` directly into a dedicated flat `s_mesh3d_queue[]` (no sorting needed; bucketed directly by `mesh_id`), leaving only true transparent 2D billboards in `s_billboard_queue[]` (precomputed sort key `(float dist_sq, uint32_t idx)` — 8 bytes instead of 56 bytes during sort!).
- **Persistent Billboard/Mesh VBOs**: Allocate once with `glBufferData(size, NULL, GL_DYNAMIC_DRAW)`, update with `glBufferSubData`, bind attrib layout once per VAO/pass, and coalesce into a single shadow + main flush per frame.
- **`libareole` Node Min-Grid Cache**: Store precomputed `float occ_cell_min[16]` (4×4 coarse conservative minima) inside `ar_node_t` once on populate (`node->rev`), rasterizing 32 triangles/node instead of 192.
- **`libareole` Height-Only Probe API**: Add `ar_probe_height_only()` skipping 4-channel RGBA bilinear interpolation; add max-heap priority queue in `ar_system_query_matrix()` with hoisted `active_view_mask`.

**Deliverables (strict abstraction-first TDD order):**
1. **`external/libareole` first**: Add `occ_cell_min[16]`, coarse 4×4 occluder rasterizer, `ar_probe_height_only()`, and heap-based refinement in `ar_system_query_matrix()`. Add unit tests verifying identical conservative occlusion and exact height parity vs. `ar_probe_view`.
2. **Flora/relief block cache + hash-first culling**: Reorder Pass 3 cheap hash rejections before noise/trig/probes; cache static lattice instances + terrain slope `(dz_e/w/s/n)` per block in `mods/nd/ux/nd_seven.c`.
3. **Separate 3D mesh & 2D billboard queues + coalesced persistent VBO flush**: Split emit queues, sort compact 8-byte keys for billboards only, coalesce `nd_seven_gl_flush_billboards()` into a single pass using `glBufferSubData` + VAOs.
4. **Populate budget & coarse Nyquist culling**: Enable a frame populate budget (`max_populates_per_frame > 0` with parent-fallback rendering) and evaluate Nyquist octave clamping for coarse LODs (L3+) without introducing shoreline height seams.
5. **Cloud pre-cull cache**: Cache per-chunk synoptic cloud coverage in `mods/nd/ux/nd_seven_gl_clouds.c` on a 0.25–0.5 s cadence (invalidated on wind/time jump).

**Exit Criteria:**
- Stationary-camera CPU frame time drops by **≥ 60%** vs. P1 baseline; `flora_probes` drops from 16k–40k/frame to ~0 steady-state.
- `make areole-lib && tests/unit/run-areole-*.sh` (or `make standalone-unit-tests`) exits `0`.
- `make -C mods/nd && make site-wasm && sh tests/unit/run-test-seven-render.sh && make test-mod MOD=nd` exits `0`.

---

## Phase 3 — GPU Shader, Fill-Rate & Pass Trimming (Wave 3)

*(Coordinates directly with `docs/CODEBASE_IMPROVEMENT_ROADMAP.md` Phase 4 GLSL deduplication so edits are made once cleanly.)*

**Problem (verified in source):**
1. **Sky drawn before opaque terrain** (`mods/nd/ux/nd_seven_gl.c:1252-1269`): `s_fs_sky` shades 100% of `render_w × render_h` (18-step torus halo raymarch + ~42 `pow()`) before terrain overwrites 50–85% of pixels.
2. **43× redundant per-tuft math in `s_vs_grass`** (`mods/nd/ux/nd_seven_gl_shaders.c:4420-4701`): Up to 32 chunks × 4,624 tufts × 43 verts = 6.36M vertex invocations/frame, each recomputing identical tuft-constant 5× `chaff_grad_noise2d`, 13× `sin`/`cos`, and 5 texture fetches.
3. **Pure-waste fragment ALU**:
   - `nd_to_lin(vec3(const))` runs `pow(max(c, 0.0), vec3(2.2))` ~45+ times in `s_fs_terrain` (~135 scalar `pow`/px), ~14× in `s_fs_sky`, ~15× in `s_fs_mesh3d`, and 4× inside the `s_fs_cloud` raymarch loop.
   - `eval_recursive_wind()` (3 `chaff_grad_noise2d`) runs 3×/pixel in `s_fs_terrain` (`1065, 1157, 1456`) and 4×/pixel in `s_fs_water` on identical `vWorldPos`.
   - Ice noise in `s_fs_water` runs unconditionally even when `freezeFactor <= 0.001`.
4. **Early-Z killer in `s_fs_mesh3d`** (`mods/nd/ux/nd_seven_gl_shaders.c:3891-4369`): 7 `discard` statements disable hardware Early-Z for *all* 3D meshes, including solid trunks and rocks.
5. **Terrain & water texture-fetch explosion**: `sampleHeightSmooth` executes 16 `texelFetch` (plus 4 extra unconditional `getH` fetches in `s_fs_water`) + up to 32 river fetches per pixel regardless of distance or quality tier.
6. **Unconditional post/blit passes**:
   - Bloom runs 3 half-res passes every frame (`mods/nd/ux/nd_seven_gl_occ.c:742-776`), un-gated by quality.
   - Up to 5 `glBlitFramebuffer` MSAA resolves/frame (`nd_seven_gl_clouds.c:478`, `nd_seven_gl_occ.c:366,679,821,1176`), including a dead/no-op CSM history blit (`nd_seven_gl_occ.c:1176`) and unused `uSceneDepth` binding in `s_fs_cloud`.

**Data Layout & Invariants:**
- **Opaque → Sky → Water → Clouds pass order**: Render opaque terrain + opaque `mesh3d` first (writing depth), then render sky with `glDepthFunc(GL_LEQUAL)`, `glDepthMask(GL_FALSE)` at `gl_Position.z = gl_Position.w` so hardware Early-Z skips all covered pixels.
- **Two `mesh3d` program variants**: Compile `s_fs_mesh3d` from shared GLSL source with `#define ND_MESH_ALPHA_TEST 0` (opaque trunks/rocks — zero `discard`, full Early-Z) and `#define ND_MESH_ALPHA_TEST 1` (canopy/foliage).
- **Per-chunk compact tuft instance buffer (`nd_grass_tuft_t`, 16–20 B/tuft)**: Precompute tuft world pos, height, yaw, wind phase, and biome tint once per chunk populate; vertex shader reads per-instance attributes instead of running 5 noises + 13 trig + 5 texture fetches 43× per tuft; switch to a 12–15 vert LOD mesh beyond ~300 wu.

**Deliverables:**
1. Move sky draw pass after opaque terrain/meshes with `GL_LEQUAL` at `z = 1.0`.
2. Pre-linearize all compile-time constant `nd_to_lin(vec3(...))` colors across `s_fs_terrain`, `s_fs_sky`, `s_fs_mesh3d`, and `s_fs_cloud`; hoist `eval_recursive_wind()` to 1 evaluation per pixel passed as an argument; guard ice noise in `s_fs_water` behind `if (freezeFactor > 0.001)`.
3. Split `s_fs_mesh3d` into opaque (no `discard`) and alpha-tested variants; bucket draw calls accordingly.
4. Remove dead `uSceneDepth` in `s_fs_cloud` and no-op CSM blit (`nd_seven_gl_occ.c:1176`); collapse redundant MSAA depth resolves into one shared resolve per frame; gate Bloom on quality preset (off on Low).
5. Add distance/quality tiering for `sampleHeightSmooth` (hardware bilinear / precomputed normal at far distance or Low/Medium quality) and drop the 4 redundant `getH` fetches in `s_fs_water`.
6. Replace per-vertex tuft generation in `s_vs_grass` with per-chunk instanced tuft buffer + mid-range LOD blade count.

**Exit Criteria:**
- Integrated-GPU / high-resolution frame time drops by **≥ 40%** vs. P2 baseline with zero shader compile/link errors in headless WebGL / native GL unit tests.
- Visual parity verified at High quality; Low/Medium presets measurably faster.
- `make -C mods/nd && make site-wasm && sh tests/unit/run-test-seven-render.sh && make test-mod MOD=nd` exits `0`.

---

## Phase 4 — Structural Architecture, Memory & Server/Protocol (Wave 4)

**Problem (verified in source):**
1. **WASM↔JS FFI overhead**: Even after P1 hoisting, individual GL calls still cross WASM→JS one by one.
2. **WASM binary & RAM footprint**:
   - `htdocs/nd.wasm` is 1.2 MB, of which `mods/nd/ux/nd_seven_gl_shaders.c` alone is 355 KB (~30%) due to unstripped GLSL comments/whitespace.
   - `static nd_field_room_memo_t s_memo[4096]` in `external/nd-world/include/nd/field.h:1339` duplicates 114 KB into every TU that includes `field.h`; `nd_seven_atlas.h` duplicates `static const` atlas tables.
   - `libareole` triple-stores vertex data (`ar_sample_t` + `values` + `rgba` = 26 B/vert, ~32 MB for 512 nodes).
   - Synchronous CPU texture baking at startup (`mods/nd/ux/nd_seven_gl.c:258-554`) causes initial load hitching.
3. **Client/Server I/O & DB churn**:
   - `mods/nd/ux/nd_state_io.c:405-408` invalidates `nd_spatial_resolver`, rebuilds `g_scene`, and dirties the minimap even on lightweight packets (`ND_BCP_TOD`, sparsity, macro view).
   - `external/nd-view/src/view.c:453-637` formats and sends 64 KB ANSI `fbcp_view_buf` to graphical clients, performs duplicate `HD_OBJ` reads, and runs up to 147 `HD_SKEL` plant lookups per move.
   - `external/axil-nd/src/law_io.c:36-122` queries `corm_get` on every `nd_write` to resolve `player_ref -> fd`.
   - `external/nd-world/src/spacetime.c:314` scans the entire `obj_hd` DB via `corm_iter` on sunrise/sunset instead of iterating active sessions.
   - `external/nd-mcp/src/mcp.c:15,319` allocates a 655 MB `static char bcp_buf[SUPERBIGSIZ]` (`80000 * 8192`) in BSS.

**Data Layout & Invariants:**
- **WebGL2 UBO + Command Ring Buffer**: Pack per-frame terrain/camera/lighting constants into a single `std140` UBO (`nd_frame_ubo_t`, uploaded once per frame); batch high-frequency draw/bind commands into a flat uint32 ring buffer in WASM linear memory flushed once per pass in `webgl-cap.js`.
- **Single-source `s_memo` & atlas tables**: Move `s_memo` into a `.c` translation unit in `nd-world` (or thread/context struct); move atlas/mesh tables out of headers (aligns with `CODEBASE_IMPROVEMENT_ROADMAP.md` P4).
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
- `htdocs/nd.wasm` shrinks by **≥ 150 KB** from GLSL stripping + dead-data cleanup; steady-state WASM heap/RAM drops by **≥ 20 MB**.
- Zero `corm_get` calls per `nd_write`; zero full-DB `corm_iter` on TOD transitions; `nd-mcp` BSS drops by ~655 MB.
- All submodule builds/tests (`areole`, `lamina`, `nd-world`, `nd-view`, `nd-mcp`, `axil-nd`), `make test-mod MOD=nd`, and `make test-fast` exit `0`.

---

## Recommended Execution Sequence

```text
1. P0 (Profiler & Baseline)          → verify: baseline ms / FFI / draw-call table recorded
2. P1 (Bridge, Leak, Hoist, -O3)     → verify: 0 uniform leak, ≥50% FFI drop, Tier-0 green
3. P2 (Flora Cache, Probes, Areole)  → verify: ≥60% CPU drop stationary, Tier-0 green
4. P3 (Sky Early-Z, Shaders, Grass)  → verify: ≥40% GPU/high-res drop, Tier-0 green
5. P4 (UBO/Ring, RAM, Server/DB)     → verify: WASM/RAM/server targets met, make test-fast green
```
