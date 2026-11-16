# TASK HANDOFF: NeverDark Field & Chunk Architecture, Bathymetry & Seven Rendering

## 1. Executive Summary & System Invariants

This document is the authoritative handoff for continuing the NeverDark procedural field, continuous bathymetry, chunk manager, and isomorphic rendering overhaul (parent quest `1xPS5i`).

### System Invariants:
- **Pure integer C everywhere**: Same C code builds as native `.so` and browser WASM (`htdocs/nd.wasm`). Zero floating-point instructions in `libnoise.so` (enforced by `objdump -d external/libnoise/lib/libnoise.so | grep -E 'xmm|fadd|fsub|fmul'`).
- **Zero new WASM imports**: `scripts/check-wasm-imports.sh` must strictly pass `W06 check PASS: all WASM imports sanctioned`.
- **Zero server GPU/EGL dependencies**: Server modules (`external/nd-world/lib/*.so`) have zero GL/EGL/DRM/Vulkan dependencies.
- **Keep existing worlds bit-identical**: Server worldgen output (`struct bio` per room: `bio_idx, tmp, rn, elevation, water_level, is_water`) is pinned by 1280 golden FNV-1a digests across worlds 0..3 (`tests/unit/data/worldgen_golden.txt`). `tests/unit/run-test-map-noise.sh` must stay 100% bit-identical.
- **Protocol unchanged**: BCP packet layout remains fixed at 604 bytes (`external/axil-nd/include/nd/bcp-types.h:105`).

---

## 2. Completed Milestones & Architectural State

### SQ1: Region Gating, Scheduling, Eviction (COMPLETED)
- **Server fallback**: `external/nd-region/src/libnd-region.c:646, 667` falls back to `"ascii-bud"` instead of `"lamina"`. Regions must explicitly opt in to canvas drivers.
- **Client gating**: `nd_map_canvas_enabled(&client_state)` in `mods/nd/ux/nd_map_ui.c` governs both the DOM canvas element `#room-map-canvas` and the redraw path. Redraw returns immediately when disabled.
- **Off websocket path**: Websocket ingest (`mods/nd/ux/nd_state_io.c:359`) only marks `g_map_dirty = 1`. Redraw occurs during `wasm_tick`.
- **Chunk construction budget**: At most `ND_CHUNK_BUILDS_PER_TICK = 2` new chunks built per tick. Placeholder flat biome color drawn until built.
- **Eviction**: `nd_seven_ground_frame_end(map)` eagerly evicts any chunk whose coordinates exit the 7×7 view window, whichever renderer is active. Disabling the canvas frees all chunks.

### SQ2: Libnoise Accessor Agreement & Fixed-Point Stepper (COMPLETED)
- **Deleted private copy**: `external/nd-world/src/noise_impl.h` deleted; `external/nd-world/include/noise_decl.h` and `src/noise.c` include `<ttypt/noise.h>` directly.
- **Exact accessor agreement**: Point sampler (`noise_sample_2d/3d`) == scanline row (`noise_scanline_eval_row`) == bulk matrix (`noise_matrix_fill_2d`) with 0 mismatches across 2D, 3D, and the int16 wrap boundary.
- **Zero hidden allocations**: Removed `malloc`/`free` fallback scratch buffers from `noise_matrix_fill_2d` and `noise_scanline_eval_row`.
- **Fixed struct layout drift**: `noise_cache_t` aliased to `noise_wcache_t`, eliminating type-punning bugs.
- **Fixed-point coordinates**: Added `chaff_coord_t { int16_t cell; uint16_t frac; }` and `chaff_fill()` supporting continuous Hermite smoothstep (`chaff_fade_frac`) and rational Q16 steps.

### SQ3: Canonical Field Header & Shared Chunk Type (COMPLETED)
- **New `<nd/field.h>`**: Pure-C header-only library in `external/nd-world/include/nd/field.h`. Unifies coarse worldgen octaves (seed `55+obits`, room coordinates, no fade) with fine terrain octaves (shifts 7..3 in world units, amplitude budget ±29).
- **Climate primitives**: `nd_field_water_level`, `nd_field_water_noise`, `nd_field_sun_dist`, `nd_field_temp`, `nd_field_rain`, `nd_field_bio_idx`, `nd_field_bio_blend` shared identically by server and client.
- **Rebased `<nd/terrain.h>`**: Removed obsolete duplicate octave tables (`ND_TERRAIN_HD_OCTAVES`, `ND_TERRAIN_HEIGHT_OCTAVES`, `ND_PLANETARY_OCTAVES`) and disconnected climate model. `nd_terrain_eval` and `nd_terrain_height` call `<nd/field.h>` directly.
- **Throughput**: 1.80 million 3D queries/sec, 9.61 million surface height queries/sec (`tests/unit/run-test-terrain-3d.sh`).

### SQ4: Seven and Minimap Render from Field (COMPLETED)
- **Box hacks deleted in `nd_seven_ground.c`**:
  * Deleted `nd_global_land_dist`, `s_active_land_tiles`, `s_active_land_juts`, `nd_coast_map_build`, `nd_coast_map_build_if_needed`, `nd_water_depth_at` (the Dean shelf profile).
- **Strict physical water depth $W - H$**:
  * Depth is computed strictly as $W - H$ ($H < W$) clamped to 28.
  * As elevation $H$ rises from the seabed to the shore, water depth decreases monotonically and hits 0 at dry land.
- **High-resolution underwater & terrestrial substrate**:
  * Substrate is rendered from continuous field samples `fs.bio`, `fs.bio2`, `fs.blend` via `ndw_biome_base_rgb` and `alpha_blend_rgb`.
  * Underwater seabed displays authentic underlying biomes illuminated by sunlight slope hillshading beneath the Beer-Lambert water column.
- **Minimap independence**:
  * `mods/nd/ux/nd_lamina.c` renders wilderness directly from `nd_field_sample` at lod 1 (16 world units/sample) with water where `s.depth > 0`.
  * Minimap builds **zero Seven chunks**, reducing minimap test/render latency from 135 seconds to 0.28 seconds.
- **Verified with 9 test suites passing (exit code 0)**:
  * `run-test-seven-render.sh`: ALL 54 TESTS PASSED (9.4s).
  * `run-test-map-render.sh`: ALL 19 TESTS PASSED (0.28s, 900/900 matching water pixels).
  * `node tests/e2e/water-harness.mjs --parity`: 108/108 frames byte-identical native vs wasm (105 distinct frames).
  * `run-test-region-gating.sh`: ALL PASSED.
  * `run-test-terrain-3d.sh`: ALL PASSED.
  * `run-test-map-noise.sh`: ALL PASSED (1280 golden rows matched bit-for-bit).
  * `run-test-ssr-hydration.sh`: ALL PASSED.
  * `run-test-seven-world.sh`: ALL PASSED.
  * `run-test-lamina-bridge.sh`: ALL PASSED.

---

## 3. Remaining Work: SQ5 (Docs, Visual Tool, Benchmarks, Full Gates)

The remaining requirements to conclude parent quest `1xPS5i`:
1. **5.1 Benchmarks**:
   - Create `tests/unit/bench_field.c` benchmarking:
     * ms per lod-3 chunk build (target: < 5 ms in WASM)
     * ms per minimap redraw (target: < 2 ms)
     * noise hash fetch counts per chunk.
2. **5.2 Visual inspection tool**:
   - `tests/unit/test_visual_locations.c` renders real worldgen for ≥12 locations (coasts, shelves, multi-biome shores, deep basins, islands) to `art/visual-inspection/*.png`.
3. **5.3 Documentation updates**:
   - Update `RECUR_NOISE.md`, `TERRAIN_ROADMAP.md`, `docs/OVERVIEW.md`, etc., noting the unified `<nd/field.h>` architecture and removal of stale duplicate cascades.
4. **5.4 Full test suite & turnkey gates**:
   - Run `make && ./test.sh` and ensure zero failures.

---

## 4. Hand-Off Resume Prompt

Copy and paste this prompt to resume immediately after compaction:

\`\`\`text
Resume Quest 1xPS5i / Sub-quest 1xPSoJ.
Read docs/handoff-field-chunk-architecture.md for complete background and state.

Current status:
- SQ1 (Region gating & budget), SQ2 (Libnoise agreement & fixed-point stepper), SQ3 (Canonical <nd/field.h> & shared chunk type) are completed and archived.
- SQ4 (Seven and minimap render from the field) is fully implemented and all verification suites are GREEN:
  * bash tests/unit/run-test-seven-render.sh
  * bash tests/unit/run-test-map-render.sh
  * node tests/e2e/water-harness.mjs --parity
  * bash tests/unit/run-test-region-gating.sh
  * bash tests/unit/run-test-terrain-3d.sh
  * bash tests/unit/run-test-map-noise.sh
  * bash tests/unit/run-test-ssr-hydration.sh
  * bash tests/unit/run-test-seven-world.sh
  * bash tests/unit/run-test-lamina-bridge.sh
  * sh scripts/check-wasm-imports.sh
  * sh scripts/check-module-boundaries.sh

Exact next steps:
1. In Quest 1xPSoJ, invoke quest_update_state({claimComplete: true}) to close SQ4.
2. Mount SQ5 (Docs, visual tool, benchmarks, full test gates):
   - Implement tests/unit/bench_field.c
   - Generate visual inspection captures in tests/unit/test_visual_locations.c -> art/visual-inspection/*.png
   - Update stale docs (RECUR_NOISE.md, TERRAIN_ROADMAP.md)
   - Run full make && ./test.sh
3. Claim complete on parent 1xPS5i and archive via quest_archive.
\`\`\`
