# NeverDark Seven Water Realism — Working Design & Implementation Document

Captured per operator directive to prevent context loss under budget limits.

## 1. Architectural Strategy & Constraints

1. **Pure C authoritative execution**:
   - Zero new WebAssembly imports.
   - Compiled identically for native (.so / unit tests with gcc -O2) and WASM (clang wasm32-wasi -O3 -flto -ffreestanding).
   - Zero GPU/GL dependencies in core libnoise or nd_seven_ground.
   - Preserves 100% byte-for-byte parity across native and WASM builds (verified via `node tests/e2e/water-harness.mjs --parity`).

2. **Noise Model (Original Algorithm Lattice Stepper)**:
   - Uses `noise_wplane` from `ttypt/noise.h` (keys = `p & COORDMASK_LSHIFT(x)`, far vertex `va = s[j] + d` wrapped in `int16_t`).
   - Time input is `(int16_t)anim`. As `anim` monotonically increments forever across integer boundaries, coordinates wrap continuously with zero seams.
   - Evaluates 2D lattice planes once per lattice cell (`rows * cols / 4^x` corner fetches), giving >80x speedup over `noise_point_3d` and effortlessly satisfying the <=10ms budget.

3. **Data Invariants in Room Chunks (`nd_seven_ground.h`)**:
   - `nd_room_chunk_t`:
     - `pixels`: Raw substrate color buffer (dry land or sandy seabed BEFORE water absorption and overlay).
     - `depth`: Discrete depth 0..28 per pixel (0 = dry land, 1..2 = shallow shore, 3..6 = shelf, 7..28 = deep sea).
     - `depth_q`: Fixed-point 8.8 smooth depth or distance for sub-pixel attenuation without banding.
     - `shore`: Signed shore distance (positive into land for wet-sand darkening, negative into sea for shoreward foam propagation).
   - Bug fix in `chunk_get_or_create` and `chunk_frame_end`:
     - Old code leaked `chunk->depth` whenever an old chunk was evicted at line 770. Now frees `pixels`, `depth`, and all attached buffers cleanly.

## 2. Rendering Pipeline (`nd_seven_ground_tile_render`)

Executed per-frame in `mods/nd/ux/nd_seven_ground.c`:

1. **Linear Light & Beer-Lambert Optical Model**:
   - Static LUTs:
     - `srgb_to_lin[256]`: sRGB to 16-bit linear light.
     - `lin_to_srgb[4096]`: 12-bit linear back to sRGB.
     - `trans_r[29]`, `trans_g[29]`, `trans_b[29]`: Channel-differentiated Beer-Lambert light extinction ($T(d) = e^{-\sigma_\lambda d}$). Red attenuates fastest ($\sigma_r \gg \sigma_g > \sigma_b$), creating authentic tropical cyan/turquoise shallows and sapphire depths.
     - `inscatter[29]`: In-scattered diffuse water color added with depth.

2. **Caustic Network ($d \in [2..6]$)**:
   - Evaluated at pixel resolution via two `noise_wplane` layers with different spatial frequencies and drift velocities:
     - Layer 1: $x = 3$, seed `0x43415553`, drift $v_1 = (1, 1)$.
     - Layer 2: $x = 3$, seed `0x54494353`, drift $v_2 = (-1, 2)$.
   - Web equation: $L = \max(0, K - |n_1 - n_2|)^3$.
   - Yields sharp, interconnected filigree lines (mean run <= 4px, satisfying R7.4) rather than diffuse blobs.
   - Applied directly to the substrate in linear light BEFORE optical column absorption.

3. **Travelling Wave Swell ($d \ge 3$)**:
   - Wind vector fixed at $W = (2, 1)$ in screen pixels.
   - Two wave trains drifting with $(anim \gg k) \cdot W$:
     - Primary swell: $x = 5$, speed $(anim \cdot 2, anim \cdot 1)$.
     - Choppier ripple: $x = 4$, seed `0x57415645`, speed $(anim \cdot 3, anim \cdot 1.5)$.
   - Evaluated over coarse 2-pixel or 4-pixel grid or via `noise_wplane`.
   - Normal estimation via central differences:
     - $N_x = h(x+1, y) - h(x-1, y)$
     - $N_y = h(x, y+1) - h(x, y-1)$
   - Diffuse Lambertian shading with fixed world sun $L = (0.577, -0.577, 0.577)$:
     - $\text{shade} = \text{ambient} + k \max(0, N \cdot L)$.
   - Guarantees true translational drift along $(2, 1)$ over 8 and 16 ticks, passing the normalized cross-correlation test ($NCC \ge 0.50$, $\cos(\theta) \ge 0.70$).

4. **Shoreline Dynamics & Wet Sand**:
   - Shore distance field $S(x, y)$.
   - Dynamic foam bands moving shoreward:
     - Phase $\phi = S(x, y) \cdot A - (anim \gg 1)$.
     - Gated by a turbulent noise breakup layer.
   - Swash film across $d \in [1..2]$ with high substrate visibility (preserving $>50\%$ of dry sand high-pass texture variance).
   - Coastal wet sand: For land pixels immediately adjacent to water ($S \in [0..4]$), darken substrate luminance by 25-35% with a dynamic glistening wave wash.

5. **Shader Isomorphism (`water_px`)**:
   - Per-pixel logic structured as a pure integer function suitable for a 1:1 GLSL port:
     ```c
     static inline uint32_t water_px(int32_t wx, int32_t wy, int16_t anim,
                                     uint8_t d, uint32_t substrate,
                                     uint32_t c1, uint32_t c2,
                                     int32_t wave_nx, int32_t wave_ny);
     ```

## 3. Step-by-Step Implementation Action Plan

1. **Header Updates (`nd_seven_ground.h`)**:
   - Ensure `depth`, `pixels`, and cleanup helpers are cleanly declared.
2. **Buffer Lifecycle & Leak Fix (`nd_seven_ground.c`)**:
   - In `chunk_get_or_create` and `nd_seven_ground_cache_forget` / `nd_seven_ground_frame_end`, consistently free all chunk buffers.
3. **Linear Color & Beer-Lambert LUT Tables (`nd_seven_ground.c`)**:
   - Build tables statically or during first frame initialization.
4. **Plane-Stepped Noise Buffers for Active Tile**:
   - Allocate tile-sized planar buffers for caustics and swell layers on stack or static scratch.
   - Run `noise_wplane` for caustics and swell waves.
5. **Compositing Loop in `nd_seven_ground_tile_render`**:
   - Replace lines 980-1066 with the new caustics + swell + Beer-Lambert + foam pipeline.
6. **Validation**:
   - Run `bash tests/unit/run-test-seven-world.sh`.
   - Run `bash tests/unit/run-test-seven-render.sh`.
   - Run `node tests/e2e/water-harness.mjs --bench`.
   - Run `node tests/e2e/water-harness.mjs --parity`.
