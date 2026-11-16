# HANDOFF: Recursive Noise Algorithm, Planetary Heightmap & Seven Water Realism (libchaff / libnoise)

**Handoff Date**: November 6, 2026  
**Status**: Ready for pickup. Phases 1, 2, 3 complete and verified; Phase 4 diagnosed with clear roadmap.  
**Canonical Master Reference**: `RECUR_NOISE.md` (root of repository)  

---

## 1. Executive Summary & Purpose

This handoff packages the comprehensive procedural world generation and realistic water overhaul for the pure-C NeverDark Seven stage and server engine, governed by the mathematical principles in `RECUR_NOISE.md`.

### Core Directive & Invariant
**DO NOT DIRTY THE BASE ALGORITHM.**
Under no circumstances may the mathematical foundation in `<ttypt/noise.h>` / `<ttypt/chaff.h>` (`noise_fix_v`, `noise_quad`, `noise_m`, `noise_wpoint_n`, `noise_wrow`, `noise_wplane`) be modified or adulterated. The algorithm is strictly pure integer arithmetic (zero floating-point instructions verified via `objdump`), seam-free across 16-bit boundaries, and dual-compiled for native OpenBSD server `.so` and freestanding WebAssembly (`htdocs/*.wasm`).

Per operator directive, the declarative engine is named **`libchaff`** with `chaff_*` prefixes, with `libnoise` and `ttypt_noise_*` preserved as full aliases for backward compatibility.

---

## 2. Completed Work & Verification Receipts

### Phase 1: Modernize libnoise / libchaff Public API
- **Location**: `external/libnoise/include/ttypt/noise.h`, `include/ttypt/chaff.h`, `src/libnoise.c`, `Makefile`, `tests/`
- **Accomplished**:
  - Declarative cascade structures: `noise_octave_t` / `chaff_octave_t`, `noise_cascade_t` / `chaff_cascade_t`, `noise_scanline_t` / `chaff_scanline_t`, `noise_cache_t` / `chaff_cache_t`.
  - Three universal accessors:
    1. `noise_matrix_fill_2d` / `chaff_matrix_fill_2d`: Macroscopic bulk worldgen fill using `noise_quad` with amortized ~0 hashes per tile and pure integer additions.
    2. `noise_scanline_init` & `noise_scanline_eval_row` / `chaff_scanline_eval_row`: Scanline row stepping fetching corners once per 3D lattice cell and stepping rows in registers.
    3. `noise_sample_2d` & `noise_sample_3d` / `chaff_sample_2d` & `chaff_sample_3d`: High-speed sparse point samplers.
  - Shared library build updated with `-mgeneral-regs-only` and `libchaff.so` symlink.
- **Verification Gate**:
  - `make -C external/libnoise test`: PASS (14/14 tests pass, including 16-bit seam wrapping and scanline corner reuse).
  - Float Invariant Audit: `objdump -d external/libnoise/lib/libnoise.so | grep -E 'xmm|fadd|fsub|fmul'` yields **ZERO** instructions.

### Phase 2: Fix Server Core to Preserve Heightmap & Biome Substrate
- **Location**: `external/nd-world/include/nd/world-types.h`, `external/nd-world/src/noise.c`, `external/nd-view/src/view.c`, `tests/unit/test_map_noise.c`
- **Accomplished**:
  - `struct bio` and `view_tile_t` updated with `bio_idx` (1..18, authentic terrestrial substrate), `is_water` (submersion flag), `elevation`, `water_level`, and `water_depth`.
  - Core worldgen bug in `noise.c:130` fixed: eliminated `r.bio_idx = _he < w ? 0 : bio_idx(r.rn, r.tmp);`. Substrate classification is **ALWAYS** preserved (`r.bio_idx = (uint8_t)bio_idx(r.rn, r.tmp)`). Submersion is computed as physical depth: `r.water_depth = r.water_level - r.elevation`.
  - Pass-through in `view.c`: `view_build_tile()` preserves `t->is_water` and `t->water_depth`; `eng_view()` sets `mt->bio_idx` with authentic substrate, setting `ND_MAP_FLAG_WATER` strictly as a status flag without erasing `bio_idx`.
- **Verification Gate**:
  - `bash tests/unit/run-test-map-noise.sh`: PASS (asserts `r.bio_idx != 0`, `r.is_water == 1`, and `r.elevation < r.water_level` for all submerged points).

### Phase 3: Physical Heightmap & Continuous Planetary Water
- **Location**: `external/nd-world/include/nd/terrain.h`, `tests/unit/test_terrain_3d.c`
- **Accomplished**:
  - Canonical 8-octave planetary cascade `ND_PLANETARY_OCTAVES[]` (continental shift 10 down to micro-relief shift 3).
  - Declarative query struct `nd_terrain_query_t` (`elevation`, `water_level`, `depth`, `slope_dx`, `slope_dy`, `is_water`).
  - Authoritative evaluators `nd_terrain_eval(world, wx, wy, anim)` and `nd_terrain_sea_level(world)`.
  - Universal level-set shorelines: $C_{\text{shore}} = \{ (wx, wy) \mid H(wx, wy) = W(wx, wy) \}$.
- **Verification Gate**:
  - `bash tests/unit/run-test-terrain-3d.sh`: PASS (verifies continuous $H(wx, wy)$ with zero vertical jumps across tile and chunk borders).

---

## 3. Deep Root-Cause Diagnosis (Why Terrain Under Water Looked Missing)

During Phase 4 investigation, the operator observed:
> *"Because it looks to me as if we lost features. Not as if we gained features. Most times I can't see the terrain under the sea near the shore."*
> *"It suggests to me that we clearly still don't have a high-resolution terrain in the seven client, which was a part of the plan."*

### Root Cause 1: Legacy 16x16 Flat Cell Rasterization was Still Active in Chunk Generation
In `mods/nd/ux/nd_seven_ground.c:980`, `chunk_get_or_create` was still calling `ground_cell()` across a $10 \times 8$ grid of coarse $16 \times 16$ world cells, drawing flat rectangles:
```c
urect(&c, 0, 0, NDW_CELL_PX, NDW_CELL_PX, base);
```
It was **never evaluating the continuous heightfield** $H_{\text{Seven}}(wx, wy)$ across the $320 \times 256$ chunk via `nd_terrain_eval()`. The client therefore lacked continuous sub-pixel elevation, slope normals, and micro-relief dunes/crags.

### Root Cause 2: Double/Triple Water Blending Drowned the Seabed Substrate
In `nd_seven_ground.c`:
1. First, `ground_cell` rendered water over substrate into `chunk->pixels` using `render_water_cell()`.
2. `s_beer_lambert_alpha[1]` had been increased from 38 to 75 (commit `59d1d74`), making shallow water ~30–50% opaque on the very first pass.
3. Second, `ground_blend_nbiomes()` blended coastal water over `chunk->pixels` again.
4. Third, `nd_seven_ground_tile_render()` read `chunk->pixels` (which already had opaque water baked in!) and blended wave highlights, shadows, and white foam on top!
Because of triple-blending and excessive alpha, the authentic seabed substrate (sand ripples, pebbles, tundra moss, volcanic basalt) was completely obscured.

### Root Cause 3: Stale Border Cache in Minimap Traversal (`test_map_render.c:534`)
In `nd_seven_ground.c`, `nd_seven_ground_cache_forget()` cleared chunk buckets and land juts, but failed to reset `s_border` (the warp grid cache) and `s_last_coast_map_pos`. When `test_map_render` modified tiles dynamically via `wild()`, stale warp grids were reused across runs. Adding `memset(s_border, 0, sizeof(s_border))` and `s_border_next = 0` resolved this state leakage.

---

## 4. Phase 4 & 5 Roadmap for the Next Agent

The next agent should execute the following targeted steps:

### Step 4.1: Viewport High-Definition Terrain Rasterization (`nd_seven_ground.c`)
In `chunk_get_or_create` (for on-screen $320 \times 256$ pixel chunks):
1. For every pixel $(x, y) \in [0..320) \times [0..256)$:
   - Compute continuous world coordinates:
     $$wx = pos[0] \cdot \text{NDW\_TILE\_W} + (x / \text{SCALE})$$
     $$wy = -pos[1] \cdot \text{NDW\_TILE\_H} + (y / \text{SCALE})$$
   - Query `nd_terrain_eval((uint16_t)pos[3], wx, wy, 0)`:
     - Ground elevation: `q.elevation`
     - Water table: `q.water_level`
     - Depth: `q.depth = q.water_level - q.elevation`
     - Hillshade multiplier from `q.slope_dx` and `q.slope_dy`:
       $$\text{shade} = \text{clamp}(256 + (-q.slope\_dx - q.slope\_dy) \cdot 12, 160, 360)$$
   - Sample authentic substrate base color from `own_biome` (desert sand, tundra rock, volcanic basalt, grass) and modulate with `shade`:
     $$\text{substrate\_col} = \text{shade\_rgb}(\text{base}, \text{shade})$$
   - Store authentic substrate in `chunk->pixels[y * TILE_PX_W + x] = substrate_col`.
   - Store physical water depth in `chunk->depth[y * TILE_PX_W + x] = (q.depth > 0 ? clamp(q.depth, 1, 28) : 0)`.

### Step 4.2: Crystal-Clear Beer-Lambert Optical Shallows
Update `s_beer_lambert_alpha` in `nd_seven_ground.c` to preserve high substrate visibility in shallows ($d \in [1..4]$):
```c
static const uint8_t s_beer_lambert_alpha[29] = {
    0,
    28,  52,  76, 100, 124, 146, 166, 182, 194, 204,
    212, 218, 224, 228, 232, 236, 239, 242, 244, 246,
    248, 249, 250, 251, 252, 253, 254, 255
};
```
- $d = 1$: alpha 28 ($89\%$ seabed transmission) — crystal clear, every grain of sand/rock visible.
- $d = 2$: alpha 52 ($80\%$ transmission) — clear Caribbean turquoise.
- $d = 3$: alpha 76 ($70\%$ transmission) — rich azure with high seabed contrast.
- $d \ge 24$: alpha 255 — deep abyssal sapphire.

### Step 4.3: Per-Frame Water Overlay Compositing
In `nd_seven_ground_tile_render()` (running each frame over `chunk->pixels`):
- Where `d > 0`:
  1. Blend transparent turquoise water over the already-rendered substrate using `water_color_at_depth(d)` and `water_alpha_at_depth(d)`.
  2. Overlay delicate caustics where $0 < d \le 12$ using row-stepped noise (`noise_scanline_eval_row` / `noise_wrow`).
  3. Overlay surf foam shoreward where $0 < d \le 3$.
  4. Diffuse wave slope lighting from fixed world sun direction.

### Step 4.4: Verification Suites
Run the following test commands to verify completion:
```bash
make -C external/libnoise test
bash external/libnoise/tests/run_tests.sh
bash tests/unit/run-test-map-noise.sh
bash tests/unit/run-test-terrain-3d.sh
bash tests/unit/run-test-seven-render.sh
node tests/e2e/water-harness.mjs --parity
bash tests/unit/run-test-map-render.sh
bash tests/unit/run-test-lamina-bridge.sh
make && ./test.sh
```

---

## 5. Master Handoff Prompt for Next Agent

```text
RESUME PROMPT:
You are continuing Quest 1xPOfD for NeverDark Seven water realism, continuous planetary heightmap, and libchaff/libnoise modernization according to RECUR_NOISE.md and docs/handoff-recursive-noise-water.md.

CURRENT PROGRESS:
- Phase 1 (libnoise / libchaff modernization with declarative cascades, zero float instructions, zero seams) is 100% COMPLETE and TESTED.
- Phase 2 (server core biome preservation in noise.c, view.c, world-types.h) is 100% COMPLETE and TESTED (run-test-map-noise.sh PASS).
- Phase 3 (planetary 8-octave cascade in terrain.h, nd_terrain_eval, level-set shorelines) is 100% COMPLETE and TESTED (run-test-terrain-3d.sh PASS).

IMMEDIATE TASK (PHASE 4 & 5):
1. In mods/nd/ux/nd_seven_ground.c, implement high-resolution continuous viewport terrain rasterization in chunk_get_or_create() using nd_terrain_eval() across the 320x256 pixel chunk.
2. Render authentic substrate with slope hillshading into chunk->pixels, and store physical depth in chunk->depth.
3. Tune s_beer_lambert_alpha so shallow water (d=1..4) is crystal-clear transparent turquoise (alpha 28..76, >70% seabed transmission), making the seabed terrain near the shore vividly visible.
4. Verify all gates:
   bash tests/unit/run-test-seven-render.sh
   node tests/e2e/water-harness.mjs --parity
   bash tests/unit/run-test-map-render.sh
   make && ./test.sh
5. Commit all work cleanly with git.
```
