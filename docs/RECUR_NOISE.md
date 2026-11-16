 PART I: The Recurring & Recursive Nature of the Noise Algorithm

 The genius of libnoise (external/libnoise/include/ttypt/noise.h; canonical noise engine, eliminating legacy noise_impl.h) is that
 recursion is built into every layer of its mathematics:
 1. Recursive Spatial Subdivision (cy < x): Sub-matrices recursively pre-step parent quad vertices (noise_fix_v).
 2. Recursive Dimensional Collapse (noise_quad): Hypercubes of dimension N are recursively collapsed down to
    dimension 0 using an integer push/pop stack.
 3. Recursive Octave Hierarchy: High-frequency micro-features nest recursively inside low-frequency macro-features.
 4. Recursive Modular Wrapping: Two's complement integer arithmetic recursively tiles the infinite universe across
    data-type boundaries without seams.

 ────────────────────────────────────────────────────────────────────────────────

 ### 1.1 The Sub-Quad Derivation: Recursive Spatial Nesting (noise_fix_v)

 The core dilemma of procedural world generation has always been: How does a client render a high-definition,
 sub-pixel view of a room without drifting away from the server’s low-resolution macroscopic world evaluation?

 In libnoise, this is solved by analytical sub-quad derivation and the unified procedural field (<nd/field.h>).

 Look at noise_m in <ttypt/noise.h>:

 ```c
   /* ms = matrix origin in world; x = feature quad size (2^x); cy = matrix size (2^cy) */
   static inline void noise_m(uint32_t *mat, int16_t *ms, unsigned x, unsigned w,
                              unsigned seed, unsigned cy, uint8_t dim) {
       uint32_t v[(1 << (dim + 1)) - 1];

       if (cy > x) {
           /* Case 1: Matrix is larger than noise quad -> tile multiple quads */
           _noise_m(mat, v, x, ms, w, seed, cy, dim);
       } else {
           /* Case 2: Matrix is smaller than noise quad -> ZOOM INSIDE THE QUAD */
           int16_t qs[dim];
           noise_get_s(qs, ms, COORDMASK_LSHIFT(x), dim); /* Find parent quad origin */
           noise_get_v(v, qs, x, w, seed, dim);            /* Fetch parent 2^dim corners */

           if (x > cy)
               noise_fix_v(v, qs, ms, x, cy, dim);         /* RECURSIVELY STEP DOWN */

           noise_quad(mat, v, cy, cy, dim);
       }
   }
 ```

 #### How noise_fix_v Works:

 When a query region (2ᶜʸ, e.g. a single room or on-screen viewport) is smaller than the macroscopic feature quad
 (2ˣ, e.g. a 1024-unit continental swell), noise_fix_v algebraically steps the parent quad's vertices down to the
 exact sub-boundaries of the smaller view:

 ```c
   static inline void __fix_v(uint32_t *v, int32_t *st, int16_t *ms, int16_t *qs,
                              unsigned x, uint16_t vl, unsigned cy) {
       register uint32_t *vn = v + vl;
       register uint16_t dd = (*ms - *qs) >> cy; /* Offset of sub-box inside parent quad */

       calc_steps(st, v, x, vl, cy);             /* Compute fractional slope across parent */

       if (dd)
           step(v, st, vl, dd);                  /* Slide vertices to sub-matrix min origin */

       memcpy(vn, v, sizeof(uint32_t) * vl);     /* Copy to opposite boundary */
       step(vn, st, vl, 1);                      /* Step by the exact side length of M */
   }
 ```

 #### Why This is Revolutionary for Our Engine:

 - When the server generates a 32 × 32 world chunk (cy = 5), it evaluates a 1024-unit continental octave (x = 10).
   noise_fix_v steps the 1024-unit quad down to the 32-unit chunk.
 - When the Seven client renders the on-screen room (320 × 256 pixels), it evaluates that exact same 1024-unit quad
   at continuous sub-coordinates.
 - The client's terrain elevation is mathematically guaranteed to be the exact continuous interpolation of the
   server's chunk corners! There is zero mathematical drift between the server's logical room rules and the client's
   visual rendering.

 ────────────────────────────────────────────────────────────────────────────────

 ### 1.2 Recursive Dimensional Collapse: The Hypercube Matrix Stepper (noise_quad)

 When filling a grid, calculating points one by one requires hashing 2ᵈⁱᵐ corners per pixel. For an N-dimensional
 space, this is catastrophically slow.

 noise_quad implements a recursive dimensional push/pop stack that collapses N dimensions down to 1D scanlines using
 pure integer additions with zero multiplies in the inner loop:

 ```
   Hypercube (dim=3: 8 vertices)
          │
          ▼ [PUSH: calc_steps along Z]
     Planes (dim=2: 4 vertices)
          │
          ▼ [PUSH: calc_steps along Y]
      Rows (dim=1: 2 vertices)
          │
          ▼ [INNERMOST LOOP: pure addition *c += vt[0]]
      Pixels (dim=0)
          │
          ▲ [POP: step along Y]
          ▲ [POP: step along Z]
 ```

 ```c
   /* In external/libnoise/include/ttypt/noise.h (formerly noise_impl.h) */
   static inline void noise_quad(uint32_t *c, uint32_t *vc, unsigned z, unsigned cy, uint8_t dim) {
       uint16_t ndim = dim - 1;
       uint16_t tvl = 1 << ndim;
       ...
       do {
           do {
               /* PUSH: Descend to inner dimension */
               cd >>= cy; ndim--; vc = vt; stc += tvl; tvl >>= 1;
   start:      ce_p[ndim] = c + (cd << z);
               vt = vc + (tvl << 1);
               calc_steps(stc, vc, z, tvl, 0);
               memcpy(vt, vc, tvl * sizeof(uint32_t));
           } while (ndim);

           /* INNERMOST LOOP: Pure addition, ~0 hashes per sample */
           for (; c < ce_p[0]; c += cd, vt[0] += *stc)
               *c += vt[0];

           do {
               /* POP: Ascend to outer dimension and step */
               ++ndim;
               if (ndim >= dim) return;
               c -= cd << z; cd <<= cy; vt = vc;
               tvl <<= 1; stc -= tvl; vc -= (tvl << 1);
               step(vt, stc, tvl, 1);
               c += cd;
           } while (c >= ce_p[ndim]);
       } while (1);
   }
 ```

 This is why the server is so fast: filling a 1024-tile chunk does not evaluate 6,144 point hashes. It evaluates a
 few corner hashes and lets noise_quad cascade through memory via pure register additions.

 ────────────────────────────────────────────────────────────────────────────────

 ### 1.3 Multi-Scale Recursive Octave Cascades (Macro Server → Micro Client)

 An octave cascade is itself a recursive self-similar summation:

                           x
       M-1                  k
 H(p) = ∑  Aₖ · noise(p · 2  )
       k=0

 Because each octave adds a higher-frequency recursive layer:
 - The Server evaluates low-frequency octaves (k ∈ [0..4], shifts 10 … 6):
     - Governs continental plates, ocean basins, regional mountain ranges, and macro water level.
     - Step size = 1 room. Evaluated in bulk via noise_matrix_fill_2d().
 - The Seven Client takes the server’s seed and coordinates, and recursively evaluates:
   $$H_{\text{Seven}}(x, y) = \underbrace{\sum_{k=0}^{4} A_k \cdot \text{noise}(x, y)}_{\text{Server Macro
   Invariant}} + \underbrace{\sum_{k=5}^{7} A_k \cdot \text{noise}(x, y)}_{\text{Client Micro Relief (dunes, crags,
   ripples)}}$$
 - The client's high-definition terrain is naturally anchored to the server's macroscopic elevation. A mountain room
   on the server has rugged peaks in Seven; an ocean trench on the server has deep submarine chasms in Seven.

 ────────────────────────────────────────────────────────────────────────────────

 ### 1.4 Recursive Modular Wrapping: The Seam-Free Invariant

 Standard noise functions evaluate cell coordinates using integer division: cell = ⌊ x / L ⌋.

 When a signed 16-bit coordinate reaches +32767 and increments to -32768, naive division flips the cell index from
 +2047 to -2048, causing the corner hashes to jump wildly.

 In libnoise, lattice coordinates are bitmasked modular coordinates:

 s = p & mask (where mask = -1 ≪ x)

 When p increments past 32767:

 32767 + 1 = -32768 (mod 2¹⁶)

 Because two's complement addition is algebraically cyclic, the far-edge vertex s + 2ˣ evaluated at +32767 wraps
 directly to the near-edge vertex at -32768.

 The noise function is infinitely recurring across the entire 16-bit integer lattice. A player walking across the
 planet, or a 32-bit water animation counter incrementing forever, will cycle continuously through time and space
 without ever hitting a seam, edge jump, or boundary crash!

 ────────────────────────────────────────────────────────────────────────────────

 PART II: Deep Reference — The Server Erasing Biome Information

 Before detailing the plan, we must confront the exact historical bug that crippled water and terrain in Seven:

 ### The Crime in external/nd-world/src/noise.c:132

 ```c
   /* In noise_full() */
   r.tmp = temp(obits, _he, _tm, s[Y_COORD] + (j >> CHUNK_Y));
   r.rn  = rain(obits, w, _he, _cl, r.tmp);

   /* THE FATAL FLAW: */
   r.bio_idx = _he < w ? 0 : bio_idx(r.rn, r.tmp);
 ```

 1. bio_idx(r.rn, r.tmp) evaluates temperature and rain to determine what the ground actually is: BIOME_TUNDRA,
    BIOME_COLD_DESERT, BIOME_VOLCANIC, BIOME_SAVANNAH, etc.
 2. If elevation _he < w, the server forces bio_idx = 0 (BIOME_WATER), wiping out the substrate classification.
 3. This erased value propagates through nd-view/src/view.c into nd_map_tile_t.bio_idx.
 4. When Seven renders the stage (nd_seven_ground.c:980–1005), it attempts to draw the seabed under the water. But
    because own_biome == 0, it has no idea what ground is there!
 5. Seven is forced to run a 9-neighbor search to guess what land is nearby, and if none is within 24 units, it
    defaults to hardcoded generic sand { NDW_G_SAND, 128, 128, 128 }.
 6. Furthermore, because water is treated as a discrete tile biome (bio_idx == 0) rather than a continuous depth
    layer, adjacent rooms with different water flags create square rectangular cutouts where water abruptly vanishes
    into dry land.

 ────────────────────────────────────────────────────────────────────────────────

 PART III: The Comprehensive, End-to-End Master Plan

 Here is the complete implementation blueprint, structured across five coordinated phases.

 ────────────────────────────────────────────────────────────────────────────────

 Phase 1: Modernize libnoise into an Ergonomic Public Engine (external/libnoise)

 Objective: Expose the recursive matrix, scanline, and point accessors through a clean, declarative C API,
 consolidating all noise engines under <ttypt/noise.h>.

 ### 1.1 Declarative Cascades (external/libnoise/include/ttypt/noise.h)

 ```c
   #ifndef TTYPT_NOISE_H
   #define TTYPT_NOISE_H

   #include <stdint.h>
   #include <stddef.h>

   /* Single noise octave specification */
   typedef struct {
       uint8_t  shift;     /* Lattice size 2^shift (e.g. 10 = 1024, 3 = 8) */
       uint16_t amp;       /* Amplitude weight in fixed-point world units */
       int8_t   skew_x;    /* Domain skew: px = x + y / skew_x (0 = disabled) */
       int8_t   skew_y;    /* Domain skew: py = y - x / skew_y (0 = disabled) */
       uint16_t contrast;  /* Fixed-point contrast multiplier (256 = 1.0x, 512 = 2.0x) */
   } noise_octave_t;

   /* Multi-scale composite cascade */
   typedef struct {
       uint32_t seed;
       uint8_t  count;
       const noise_octave_t *octaves;
   } noise_cascade_t;

   /* Corner cache for scanline row stepping */
   typedef struct {
       int16_t  s[4];
       uint32_t v[16];
       uint32_t fetches;
       uint8_t  valid;
   } noise_cache_t;

   typedef struct {
       noise_cache_t caches[16];
       const noise_cascade_t *cfg;
       uint8_t dim;
   } noise_scanline_t;

   /* --- The Three Universal Accessors --- */

   /* 1. Bulk Matrix Stepper (Server Worldgen: fills 2^cy x 2^cy chunk in bulk)
    * Amortized ~0 hashes per tile; uses pure integer additions. */
   void noise_matrix_fill_2d(const noise_cascade_t *cfg,
                             int16_t origin_x, int16_t origin_y,
                             uint8_t size_shift, int32_t *out_matrix);

   /* 2. Scanline Stepper with Corner Cache (Seven Water & Ground Rasterizer)
    * Fetches corners once per 3D lattice cell; steps rows in registers. */
   void noise_scanline_init(noise_scanline_t *scan, const noise_cascade_t *cfg, uint8_t dim);

   void noise_scanline_eval_row(noise_scanline_t *scan,
                                int16_t start_x, int16_t y, int16_t z,
                                size_t count, int32_t *out_row);

   /* 3. Sparse Point Sampler (Collision, Entrances, Caverns) */
   int32_t noise_sample_2d(const noise_cascade_t *cfg, int16_t x, int16_t y);
   int32_t noise_sample_3d(const noise_cascade_t *cfg, int16_t x, int16_t y, int16_t z);

   #endif /* TTYPT_NOISE_H */
 ```

 ### 1.2 Verification Gate

 - Build: make -C external/libnoise
 - Test: bash external/libnoise/tests/run_tests.sh
 - Invariant Audit: Run objdump -d external/libnoise/lib/libnoise.so | grep -E 'xmm|fadd|fsub|fmul' to prove zero
   floating-point instructions.

 ────────────────────────────────────────────────────────────────────────────────

 Phase 2: Fix Server Core to Preserve Heightmap & Biome (external/nd-world)

 Objective: Ensure the server retains authentic ground biomes and continuous physical elevation/water tables, never
 destroying them when submerged.

 ### 2.1 Update Data Structures (external/nd-world/include/nd/world-types.h)

 ```c
   struct bio {
       coord_t   tmp;           /* Temperature */
       ucoord_t  rn;            /* Rainfall */
       uint32_t  ty;
       uint8_t   bio_idx;       /* ALWAYS authentic substrate biome (1..18) */
       uint8_t   is_water;      /* 1 if submerged (elevation < water_level), else 0 */
       coord_t   elevation;     /* Ground surface elevation */
       coord_t   water_level;   /* Water table surface elevation */
       coord_t   water_depth;   /* Depth = max(0, water_level - elevation) */
       unsigned  raw[16];
   };
 ```

 ### 2.2 Fix Core Worldgen Loop (external/nd-world/src/noise.c:130)

 ```c
   for (j = 0; j < CHUNK_M; j++) {
       struct bio r = chunks_bio_raw[i + j];
       uint32_t _cl = n_cl[j], _tm = n_tm[j];
       register uint32_t _he = n_he[j], w = water_level(obits);

       r.tmp = temp(obits, _he, _tm, s[Y_COORD] + (j >> CHUNK_Y));
       r.rn  = rain(obits, w, _he, _cl, r.tmp);

       /* FIX: Preserve authentic substrate ALWAYS */
       r.bio_idx = bio_idx(r.rn, r.tmp);

       /* Physical signed elevation and water table in world units */
       r.elevation   = (coord_t)((int64_t)_he * 200 / NOISE_MAX - 100);
       r.water_level = (coord_t)((int64_t)w * 200 / NOISE_MAX - 100);

       /* Submersion is physical depth, not an eraser */
       if (r.elevation < r.water_level) {
           r.is_water    = 1;
           r.water_depth = r.water_level - r.elevation;
       } else {
           r.is_water    = 0;
           r.water_depth = 0;
       }

       r = nd_evt_noise(r, _he, w, _tm, _cl);
       chunks_bio_raw[i + j] = r;
   }
 ```

 ### 2.3 Pass-Through in view.c (external/nd-view/src/view.c)

 - In view_build_tile(): ensure t->elevation, t->water_level, and authentic t->bio_idx pass through cleanly to
   view_tile_t.
 - When constructing nd_map_tile_t:
     - mt->bio_idx carries the authentic ground substrate (sand, rock, tundra, forest).
     - Set ND_MAP_FLAG_WATER strictly as a flag when submerged, never overwriting bio_idx.

 ### 2.4 Verification Gate

 - Test: bash tests/unit/run-test-map-noise.sh
 - Assertion: For all submerged test points, assert r.bio_idx != 0 and r.elevation < r.water_level.

 ────────────────────────────────────────────────────────────────────────────────

 Phase 3: Physical Heightmap & Continuous Planetary Water (nd_terrain.h)

 Objective: Provide a headless, continuous planetary heightmap and water engine shared between server BE and Seven
 client.

 ### 3.1 Declarative Planetary Cascade (external/nd-world/include/nd/terrain.h)

 ```c
   #ifndef ND_TERRAIN_H
   #define ND_TERRAIN_H

   #include <stdint.h>
   #include <ttypt/noise.h>

   /* The 8-octave planetary cascade */
   static const noise_octave_t ND_PLANETARY_OCTAVES[] = {
       { .shift = 10, .amp = 95, .skew_x = 4, .skew_y = 3, .contrast = 256 }, /* Continental */
       { .shift =  9, .amp = 48, .skew_x = 5, .skew_y = 4, .contrast = 256 }, /* Sub-continental */
       { .shift =  8, .amp = 26, .skew_x = 6, .skew_y = 5, .contrast = 256 }, /* Room-scale bays */
       { .shift =  7, .amp = 14, .skew_x = 7, .skew_y = 6, .contrast = 256 }, /* Coves & hills */
       { .shift =  6, .amp =  8, .skew_x = 8, .skew_y = 7, .contrast = 256 }, /* Lagoons */
       { .shift =  5, .amp =  4, .skew_x = 0, .skew_y = 0, .contrast = 256 }, /* Sandbars */
       { .shift =  4, .amp =  2, .skew_x = 0, .skew_y = 0, .contrast = 256 }, /* Dune crests */
       { .shift =  3, .amp =  1, .skew_x = 0, .skew_y = 0, .contrast = 256 }, /* Micro-relief */
   };

   typedef struct {
       int16_t elevation;      /* Surface ground height H(wx, wy) */
       int16_t water_level;    /* Planetary water surface W(wx, wy, time) */
       int16_t depth;          /* Water depth = W - H (depth <= 0 is dry land) */
       int16_t slope_dx;       /* Terrain gradient dH/dx */
       int16_t slope_dy;       /* Terrain gradient dH/dy */
       uint8_t is_water;       /* Convenience flag: depth > 0 */
   } nd_terrain_query_t;

   /* Single authoritative query for any continuous point on the planet */
   nd_terrain_query_t nd_terrain_eval(uint16_t world, int32_t wx, int32_t wy, uint32_t anim);

   int16_t nd_terrain_sea_level(uint16_t world);

   #endif /* ND_TERRAIN_H */
 ```

 ### 3.2 Universal Level-Set Shorelines

 Shorelines are defined as the zero-crossing level set:

 Cₛₕₒᵣₑ = { (wx, wy) ∣ H(wx, wy) = W(wx, wy) }

 - Because H and W are continuous across the entire planet, shorelines naturally meander across room borders with
   zero seams or square corners.

 ### 3.3 Verification Gate

 - Test: bash tests/unit/run-test-terrain-3d.sh
 - Assertion: Verify that H(wx, wy) is continuous across all tile borders with zero vertical jumps.

 ────────────────────────────────────────────────────────────────────────────────

 Phase 4: Seven Renderer: Continuous Viewport Heightmap & Water (mods/nd/ux)

 Objective: Eliminate squircle distance transforms in Seven, render authentic substrate under water, and drive waves
 and foam from real physical depth.

 ### 4.1 Delete Squircle Hacks (mods/nd/ux/nd_seven_ground.c)

 - Remove ndw_tile_depth().
 - Remove 9-neighbor nearest-land guessing loops (nd_seven_ground.c:980–1005).
 - Remove squircle corner rounding logic.

 ### 4.2 Viewport Continuous Rasterization

 For each on-screen room chunk (320 × 256 pixels):
 1. Render Authentic Ground Substrate:
     - Query nd_terrain_eval(world, wx, wy, anim).
     - Store physical water depth in depth_buffer[y * TILE_PX_W + x].
     - Sample authentic substrate texture (sand, tundra, basalt) based on the core biome.
     - Apply sunlight hillshading from slope_dx and slope_dy.
 2. Physical Water Layering:
     - For every pixel where depth > 0:
         - Apply Beer-Lambert optical transmission: shallow water (d ∈ [1..4]) is transparent turquoise; deep water
           (d > 24) is deep ocean blue.
         - Surf Foam: Triggered where 0 < depth ≤ 3.
         - Caustics: Evaluated via 3D noise row stepping (noise_scanline_eval_row) only where 0 < depth ≤ 12.
         - Waves: 3D animated macro swells ride on top of the water surface.
 3. Dynamic Tides / Ebb & Flow:
     - Modulate W(t) = W_base + sin(ω t) · Δ W.
     - The water naturally advances up the beach slopes and recedes, exposing wet sand, without touching any
       geometry.

 ### 4.3 Verification Gate

 - Test: bash tests/unit/run-test-seven-render.sh
 - Test: node tests/e2e/water-harness.mjs --parity (verifies 100% byte-for-byte parity between native C and WASM).

 ────────────────────────────────────────────────────────────────────────────────

 Phase 5: Lamina Minimap Integration (mods/nd/ux/nd_lamina.c)

 Objective: Ensure the Lamina minimap is an authentic, seamless downsampled representation of the Seven stage with
 zero duplicate computation.

 ### 5.1 Direct Chunk Downsampling

 In nd_lamina_render_map_live():
 - Ensure the minimap samples directly from Seven's rendered room chunks (nd_seven_chunk_ensure(map, x, y)).
 - The minimap displays the exact same continuous shorelines and water depths as the high-res stage with zero
   duplicate noise math.

 ### 5.2 Verification Gate

 - Test: bash tests/unit/run-test-map-render.sh
 - Test: bash tests/unit/run-test-lamina-bridge.sh

 ────────────────────────────────────────────────────────────────────────────────

 Final Quality Gate Matrix

 ┌─────────┬─────────────────────────────────────┬──────────────────────────────────────────────────────────────────┐
 │ Step    │ Test Suite                          │ Pass Criteria                                                    │
 ├─────────┼─────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤
 │ Phase 1 │ external/libnoise/tests/test_noise. │ 0 seams across 16-bit limits; 0 float instructions.              │
 │         │ c                                   │                                                                  │
 ├─────────┼─────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤
 │ Phase 2 │ tests/unit/test_map_noise.c         │ Submerged rooms preserve authentic substrate biome.              │
 ├─────────┼─────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤
 │ Phase 3 │ tests/unit/test_terrain_3d.c        │ Continuous H(x, y) and W(x, y) across entire planet.             │
 ├─────────┼─────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤
 │ Phase 4 │ tests/unit/test_seven_render.c      │ 0 squircle distance hacks; physical depth drives water; zero     │
 │         │                                     │ room seams.                                                      │
 ├─────────┼─────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤
 │ Phase 4 │ tests/e2e/water-harness.mjs         │ 100% native/WASM parity; 0 WebGL/host dependencies.              │
 ├─────────┼─────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤
 │ Phase 5 │ tests/unit/test_map_render.c        │ Minimap coastlines match Seven stage pixel-for-pixel.            │
 ├─────────┼─────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤
 │ Final   │ make && ./test.sh                   │ Zero build warnings, zero errors, full suite passes.             │
 └─────────┴─────────────────────────────────────┴──────────────────────────────────────────────────────────────────┘

 This is the comprehensive, end-to-end plan. It honors the recursive nature of the noise engine, fixes the core data
 erasure at the source, and delivers continuous planetary water with clean, decoupled APIs.
