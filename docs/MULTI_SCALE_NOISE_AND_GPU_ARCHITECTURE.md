# Multi-Scale Noise, Client GPU Acceleration & Stratosphere Architecture

**Document Type**: Canonical Architecture Specification  
**Status**: Authoritative Reference  
**Scope**: `libchaff` (`external/libnoise`), Web HAL (`external/liblamina`), NeverDark Seven (`mods/nd/ux`), Server Engine (`external/nd-world`)

---

## 1. Executive Vision & Invariants

This document records the comprehensive architectural design for multi-resolution procedural noise, zero-dependency server execution, client hardware GPU acceleration, and continuous scaling from sub-unit ocean caustics all the way to orbital planetary horizons.

### The Non-Negotiable Core Invariants
1. **The Server's Right Not to Sell GPU**:
   The NeverDark server (`nd.so`, `libaxil-nd.so`) runs strictly as pure software C99 integer math (`-mgeneral-regs-only`, zero floats, zero GPU libraries).
   - Zero `#include <GL/gl.h>` or `<EGL/egl.h>`.
   - Zero linking against `-lGL`, `-lEGL`, or `-lgbm`.
   - The server must compile and run on minimal, headless OpenBSD and Linux cloud instances that do not have OpenGL or Mesa installed.
2. **Client Hardware Acceleration**:
   Clients with GPU capabilities (browser WebGL2 via Web HAL and native Linux devices via `liblamina`'s headless EGL pbuffer/framebuffer) evaluate noise on GPU fragment/compute cores, eliminating single-threaded WASM CPU bottlenecks.
3. **Bit-Exact Level-Set Equivalence**:
   The GPU shader and the server CPU code must evaluate the exact same 32-bit corner hashes and Hermite interpolation. The shoreline level set $C_{\text{shore}} = \{ (wx, wy) \mid H(wx, wy) = W \}$ must match identically so client visuals never drift from server collision logic.

---

## 2. The Integer Math Superpower in Procedural Worlds

Integer math is frequently mischaracterized as a historical constraint or an accuracy compromise. In procedural world generation, **pure integer math is a profound superpower that floating-point engines can never replicate**:

### A. Bit-Exact Determinism Across Platforms
IEEE 754 floating-point operations are notoriously non-deterministic across CPU architectures, compilers, optimization levels (`-O2` vs `-O3`), and GPU hardware:
* Fused multiply-add (FMA) instructions, intermediate 80-bit x87 registers, and varying transcendental precision produce slight variations in floating-point heights.
* In a multiplayer procedural world, a $0.0001$ float discrepancy across an ocean shoreline level-set ($H = W$) causes desynchronization: one client renders dry land while the server registers drowning water collision.
* **Integer Guarantee**: 32-bit and 64-bit integer bitwise operations (`^`, `<<`, `>>`, `*`) are mathematically and provably identical on every modern processor on Earth, ensuring 100% deterministic simulation across x86_64, ARM64, WASM32, and WebGL2 GLSL ES 3.0 shaders.

### B. Free Modular Cyclic Planetary Topology
Planetary coordinate wrapping in floating-point engines requires explicit branch conditions (`if (x > MAX) x -= SPAN`), introducing branch mispredictions in tight inner loops:
* In `int16_t` two's-complement arithmetic, coordinates wrap continuously across $[-32768 \dots 32767]$ in zero CPU instructions:
  $$\text{int16\_t}(32767 + 1) = -32768$$
* This wraps the coordinate space into a continuous, seamless planetary torus at zero ALU cost and zero branches.

### C. Multi-Channel Geological Profiling in Single 32-Bit Registers
When `chaff_hash2` or `XXH32` hashes a lattice corner, it computes a full 32-bit digest. Instead of discarding 24 bits and keeping only 8 bits for elevation, integer bitfields allow multi-channel geological profiling in a single instruction pipeline:
* **Bits 0..11**: High-precision elevation (Q12.4 fixed-point).
* **Bits 12..17**: **Rock Hardness / Erosion Resistance** (governing sedimentary strata and cliff steepness).
* **Bits 18..23**: **Porosity & Moisture Retention** (governing soil moisture, vegetation clinging, and groundwater).
* **Bits 24..31**: **Micro-roughness Seed** (governing procedural pebbles, scree, and surface scatter).

---

## 3. Deep Diagnosis: Root Causes of the "Layered Cake" Terracing Artifact

When viewing procedural terrain in NeverDark's 2.5D diorama (55° pitch oblique camera with dynamic normal hillshading), terrain exhibited distinct flat horizontal terraces and stepped cliffs. An exhaustive diagnosis reveals four compounding causes:

### 1. Premature Integer Division in `noise_wreduce` (`external/libnoise/include/ttypt/noise.h:338-348`)
`noise_wstep` computed the slope per pixel by shifting right before multiplying:
```c
static inline int32_t noise_wstep(uint32_t v0, uint32_t v1, unsigned x) {
    return (int32_t)(((int64_t)v1 - (int64_t)v0) >> x); /* TRUNCATES BEFORE MULTIPLY! */
}
```
* If the elevation difference between two lattice corners is less than the cell width ($|v_1 - v_0| < 2^x$), $(v_1 - v_0) \gg x = 0$.
* The intermediate slope was truncated to **exactly zero**, creating a 100% flat horizontal plateau across the entire cell. Any remainder was lost, resulting in a sudden cliff jump at the cell boundary.

### 2. Value Noise Zero-Derivative Lattice Resonance (`external/libnoise/include/ttypt/chaff.h:77`)
Standard Value Noise interpolates scalar corner values using the cubic Hermite smoothstep fade $u_3(t) = 3t^2 - 2t^3$.
Its derivative is:
$$u_3'(t) = 6t(1 - t) \implies u_3'(0) = 0, \quad u_3'(1) = 0$$
* At every integer lattice coordinate $(x, y)$, the slope along both axes is **identically zero**.
* Because octaves use power-of-two shifts (shifts 4..8), the zero-derivative stationary points align on a regular Cartesian grid, creating a resonant matrix of flat shelves connected by steep ramps.

### 3. Integer Texture `gl.NEAREST` Stair-Stepping in WebGL2 (`external/liblamina/htdocs/lamina-bridge.js:444,785`)
* The terrain heightmap is uploaded as an integer texture (`gl.R16I`).
* In OpenGL ES 3.0 / WebGL 2, **integer texture formats disallow hardware linear filtering** (`gl.LINEAR`); the driver forces `gl.NEAREST`.
* In `VS_TERRAIN_3D`, `texture(uHeightTex, a_grid_uv).r` jumps in discrete steps from integer to integer across triangle mesh vertices. Central-difference normals (`hR - hL`, `hU - hD`) collapse to $(0, 0, 1)$ inside texels and explode into violent normal spikes at texel borders.

### 4. Coarse Height Quantization (1 World Unit per Step)
* Chunk elevation buffers stored height as raw 1-pixel integers (`int16_t`).
* On a gentle slope of 4 units over 40 pixels, each elevation value stayed identical for 10 full pixels before stepping up, guaranteeing visible 10-pixel wide terraces.

---

## 4. The Mathematical & Algorithmic Solutions

### A. Fixed-Point Arithmetic Perfection (64-Bit Multiply-Then-Shift)
In `external/libnoise/include/ttypt/noise.h`, linear interpolation must multiply *first*, then shift:
$$v(f) = v_0 + \frac{(v_1 - v_0) \cdot f}{2^x} \quad \implies \quad v_0 + \left( ((v_1 - v_0) \cdot f) \gg x \right)$$
Using 64-bit intermediate accumulators (`int64_t`), intermediate slopes never truncate to zero. For a difference of 1 unit across a 256-pixel cell, the output strictly increments as $f$ increases, guaranteeing monotonicity down to 1 LSB.

### B. $C^2$ Continuous Quintic Hermite Fade
For rugged, high-fidelity terrain, we introduce Ken Perlin's quintic polynomial $u_5(t) = 6t^5 - 15t^4 + 10t^3$:
$$u_5'(t) = 30t^2(t - 1)^2 \implies u_5'(0) = 0, \quad u_5'(1) = 0$$
$$u_5''(t) = 60t(2t^2 - 3t + 1) \implies u_5''(0) = 0, \quad u_5''(1) = 0$$
Because the second derivative is zero at cell boundaries, surface curvature and lighting normals are $C^2$ continuous across cell transitions, completely eliminating lighting creases.

### C. Anti-Resonance via Golden Ratio Rotational Octave Skew
To break Cartesian grid resonance without floating-point trigonometric functions (`sin`, `cos`):
Between successive octaves, rotate coordinates by an angle $\theta$ whose tangent is irrational, derived from the Golden Ratio ($\theta \approx 31.7^\circ$):
$$\begin{pmatrix} x' \\ y' \end{pmatrix} = \begin{pmatrix} (27874 \cdot x - 17227 \cdot y) \gg 15 \\ (17227 \cdot x + 27874 \cdot y) \gg 15 \end{pmatrix}$$
Because 27,874 and 17,227 are coprime with power-of-two grid sizes, **octave grid lines never align**. The zero-derivative stationary points are scattered irrationally across the landscape, shattering the plateau grid.

### D. Continuous Gradient & Curl Domain Warping
Using `chaff_sample_grad_2d()`, evaluate the analytical macro gradient $\nabla H = (\frac{\partial H}{\partial x}, \frac{\partial H}{\partial y})$ and displace fine octave coordinates orthogonally along the slope contour:
$$\mathbf{p}_{\text{fine}} = \mathbf{p} + \alpha \begin{pmatrix} -\partial H / \partial y \\ \partial H / \partial x \end{pmatrix}$$
This naturally folds ridges into rugged mountain crests and carves river valleys without evaluating any additional noise hashes.

---

## 5. Smoothness Across Data Type Boundaries (Q12.4 Fixed-Point Precision)

To achieve continuous vertical slopes without altering storage footprints:
* **Inside `libchaff` & Engine**: Elevation is stored as **Q12.4 fixed-point** in `int16_t` buffers (12 integer bits, 4 fractional bits = 16 sub-units per pixel, $0.0625$ unit resolution).
* **Legacy Physics / Collision Boundary**: Whole-block collision checks retrieve integer height via bit-shift `(elevation >> 4)`.
* **Client GPU / 3D Diorama Boundary**: The vertex shader receives the 16-bit integer texture and multiplies by $\frac{1}{16}$ (`0.0625`).
* **Result**: On a 4-unit slope across 40 pixels, the height increments by $1.6$ sub-units on **every single pixel**. The 2.5D diorama camera sees a smooth, continuous natural ramp with zero flat steps.

---

## 6. Deep Intelligence in Pure Integer Math

### A. Authentic Geological Strata (Terraces on Purpose)
In nature, terrain has horizontal layers, but they are sedimentary rock strata (like the Grand Canyon). We create authentic geological strata with bitwise masks:
```c
uint16_t strata_band = (elevation >> 3) & 0x7;
int16_t hardness = s_rock_hardness[strata_band];
```
Hard limestone layers resist erosion and form vertical caprock cliffs; soft shale layers underneath form gentle 35° talus slopes. Driven purely by integer shifts, terrain looks geologically sculpted.

### B. Analytical Integer Gradients & Dynamic Talus Slopes
`chaff_sample_grad_2d()` calculates exact surface slopes $\nabla H = (\frac{\partial H}{\partial x}, \frac{\partial H}{\partial y})$ using the Hermite derivative $6f(2^x - f)$ in CPU registers without finite differences:
1. **Talus Angle of Repose**: If $|\nabla H| > \text{threshold}$ (exceeding $\approx 34^\circ$), loose sand and gravel cannot rest. The shader/generator replaces green grass with exposed bedrock or scree gravel.
2. **Hydraulic Runoff**: Rainwater flows strictly in the direction of $-\nabla H$, allowing procedural stream rivulets and valley moisture accumulation.
3. **Wave Refraction**: Incoming waves slow down in shallow water and refract to face the shoreline, calculated directly from $\nabla (W - H)$.

---

## 7. Multi-Scale Physical Bathymetry & Water Simulation

### Why Water is the Planetary Flagship
1. **Unified Bathymetry ($W - H$)**:
   Water depth is physical elevation distance $d = \max(0, W - H)$. Because $W$ is planet-wide, coastlines are exact zero-crossings ($H = W$) with zero seams between server physics and client visuals.
2. **Beer-Lambert Optical Absorption Across Altitudes ($I = I_0 e^{-\mu d}$)**:
   * **Beach scale ($d \le 4$)**: Crystal turquoise shallows where submerged sand ripples and underwater topography are clearly visible.
   * **Aerial / Minimap scale ($d \le 12$)**: The continental shelf margin glows as a radiant Caribbean turquoise ribbon bordering deep midnight navy oceanic trenches.
   * **Stratosphere scale**: Capillary waves dissolve via Nyquist filtering, turning the ocean into a smooth, reflective planetary mirror.
3. **Shoreline Dynamics**:
   Incoming wave crests sharpen in the shallows, while dynamic swash/backwash foam froths across the wet sand transition zone ($d \le 2$).

---

## 8. Planetary Horizon & Stratosphere Architecture

### The Altitude Spectrum (0m to Space)

| Altitude Range | Scale Step | Visual Characteristics | Noise Behavior |
| :--- | :--- | :--- | :--- |
| **0 – 100 m (Player Scale)** | $\Delta = 1$ | Subsurface caustics, wave crest highlights, swash/backwash shore foam | All 8 octaves active (shifts 3..10) |
| **100 – 2,000 m (Aerial Scale)** | $\Delta = 16$ | Grand rolling swells (shifts 7..10), continental shelf turquoise margins | Capillary ripples (shifts 3..5) attenuate via Nyquist cutoff |
| **2,000 – 30,000 m (Stratosphere)** | $\Delta = 256$ | Smooth ocean mirror, bathymetric shelf outlines, atmospheric Rayleigh haze | Micro-waves dissolve; macro swells dominate; zero noise crawl |
| **> 30,000 m (Orbit / Space)** | $\Delta > 2048$ | Specular sun glint ellipse, curved planetary horizon, black sky boundary | Flat reflective water with global sun reflection |

### Atmospheric Rayleigh Scattering & Horizon Curvature:
1. **Quadratic Horizon Curvature**:
   In the 3D diorama vertex shader, the flat Cartesian world lattice is projected over a physical planetary sphere:
   $$Z_{\text{curved}} = H(wx, wy) - \frac{(wx - \text{cam}_x)^2 + (wy - \text{cam}_y)^2}{2 R_{\text{planet}}}$$
   Bends the horizon into space without altering database coordinates.
2. **Atmospheric Rayleigh Scattering**:
   $$\mathbf{C}_{\text{final}} = \text{mix}\left(\mathbf{C}_{\text{surface}}, \mathbf{C}_{\text{sky}}, 1.0 - e^{-\beta \cdot Z}\right)$$
   Fades distant lowlands into atmospheric pale blue while mountain peaks pierce into clear air.
3. **Sun Glint Specular Ellipse**:
   Calculates high-altitude specular sun reflection across thousands of kilometers of ocean surface.

---

## 9. Client GPU Acceleration vs. Server Zero-OpenGL Invariant

```
+-------------------------------------------------------------+
|           libchaff C99 Core (ttypt/chaff.h)                 |
|   - 100% integer math (-mgeneral-regs-only)                 |
|   - Zero GL symbols, zero GL headers                        |
+------------------------------+------------------------------+
                               |
                               v
+-------------------------------------------------------------+
|        Header-Only Shader Contract (ttypt/chaff_glsl.h)     |
|   - Canonical GLSL ES 3.0 source strings as C macros        |
|   - Bit-exact XXH32 integer hash (chaff_hash2)              |
|   - Declarative uniform layout: chaff_gl_uniforms_t         |
+------------------------------+------------------------------+
                               |
            +------------------+------------------+
            v                                     v
+-----------------------+             +-----------------------+
|  Web Client (WASM)    |             |  Linux Native Client  |
|  - WebGL2 fragment    |             |  - liblamina fb.c     |
|  - lamina-bridge.js   |             |  - Headless EGL ctx   |
|  - Render to FBO      |             |  - Pbuffer offscreen  |
+-----------------------+             +-----------------------+
```

### Manual 4-Tap Bilinear Height & Analytical Normal Derivation in WebGL2:
In `external/liblamina/htdocs/lamina-bridge.js`, `VS_TERRAIN_3D` performs manual bilinear reconstruction of integer height textures:
```glsl
vec2 texSize = vec2(textureSize(uHeightTex, 0));
vec2 pixelCoord = a_grid_uv * texSize - 0.5;
ivec2 iCoord = ivec2(floor(pixelCoord));
vec2 f = fract(pixelCoord);
vec2 w = f * f * (3.0 - 2.0 * f);

float h00 = float(texelFetch(uHeightTex, clamp(iCoord + ivec2(0, 0), ivec2(0), ivec2(texSize) - 1), 0).r);
float h10 = float(texelFetch(uHeightTex, clamp(iCoord + ivec2(1, 0), ivec2(0), ivec2(texSize) - 1), 0).r);
float h01 = float(texelFetch(uHeightTex, clamp(iCoord + ivec2(0, 1), ivec2(0), ivec2(texSize) - 1), 0).r);
float h11 = float(texelFetch(uHeightTex, clamp(iCoord + ivec2(1, 1), ivec2(0), ivec2(texSize) - 1), 0).r);

float unpackScale = (uHeightIsQ12_4 != 0.0) ? 0.0625 : 1.0;
h00 *= unpackScale; h10 *= unpackScale; h01 *= unpackScale; h11 *= unpackScale;

float wz = mix(mix(h00, h10, w.x), mix(h01, h11, w.x), w.y) * uHeightScale;

// Analytical surface normals from bilinear patch partial derivatives
float dHdX = 6.0 * f.x * (1.0 - f.x) * mix(h10 - h00, h11 - h01, w.y) * (texSize.x / uChunkSpan.x);
float dHdY = 6.0 * f.y * (1.0 - f.y) * mix(h01 - h00, h11 - h10, w.x) * (texSize.y / uChunkSpan.y);
vec3 N = normalize(vec3(-dHdX, -dHdY, 1.0));
```
**Result**: Completely eliminates terracing ridges, stair-steps, and harsh normal spikes.

---

## 10. Engine Codebase Audit & Bulk Performance Overhaul

A critical audit of the codebase reveals multiple efficiency mismatches:

| Subsystem | Location | Current Algorithm | Assessment | Correct Primitive |
| :--- | :--- | :--- | :--- | :--- |
| **Ground Chunk Rasterization** | `mods/nd/ux/nd_seven_ground.c:755` | 20,480 calls to scalar `nd_field_sample()` | **Severe Inefficiency**: Re-hashes corners point-by-point (>400,000 hashes/chunk) | **`chaff_fill()`** or **`noise_wplane()`** (hashes once per cell, steps in registers) |
| **Minimap Chunk Fill** | `external/nd-world/include/nd/field.h:383` | Nested loop of scalar `nd_field_sample()` | **Sub-optimal**: Point queries across uniform grid | **`chaff_fill_lod()`** (band-limited matrix stepper) |
| **Biome Border Warping** | `external/nd-seven/include/nd/seven-world.h:589` | Bespoke `ndw_vnoise()` and `ndw_lattice()` | **Custom Redundancy**: Separate value-noise implementation | Can be unified into canonical `libchaff` |
| **Water Waves (2D CPU)** | `mods/nd/ux/nd_seven_ground.c:984` | `noise_wrow()` with `noise_wcache_t` | **Efficient**: Hashes once per row, steps in registers | Retain current scanline pattern |
| **Water Waves (3D WebGL)** | `external/liblamina/htdocs/lamina-bridge.js:140` | Screen-space `sin((vUV.x + vUV.y) * 20.0...)` | **Placeholder**: Not using procedural noise at all | **`chaff_sample_octave`** via `<ttypt/chaff_glsl.h>` |
| **Player Elevation & Collision** | `mods/nd/ux/nd_seven_ground.c:1173` | `nd_seven_ground_elevation()` | **Optimal**: Scalar point query at dynamic coordinates | Scalar point sampler (`chaff_sample_2d`) |

### Key Optimization Win:
Switching chunk generation in `chunk_get_or_create()` from 20,480 scalar queries to bulk scanline/matrix steppers (`chaff_fill_lod`) amortizes corner fetches to ~0 hashes per pixel, speeding up chunk generation by **10x to 15x** and eliminating the bottleneck that throttled chunk generation to 2 chunks per tick.

---

## 11. Phased Evolution Roadmap & Current Completion State

1. **Phase 1: Foundation & Public API (COMPLETE)**:
   * Modernized `<ttypt/chaff.h>` with `chaff_fill_lod()`, `chaff_sample_grad_2d()`, and `chaff_coord_scale()`.
   * Exported header-only GLSL ES 3.0 shader contract (`<ttypt/chaff_glsl.h>`) with bit-exact `XXH32` integer hash (`chaff_hash2`).
   * 25 automated unit tests passing with zero warnings in `external/libnoise/tests/test_noise.c`.

2. **Phase 2: Mathematical Precision & Anti-Resonance in Library (COMPLETE in libnoise/libchaff; ACTIVE in field.h)**:
   * **COMPLETE in `libnoise`**: Replaced premature division in `noise_wstep` / `noise_wreduce` with 64-bit multiply-then-shift `v0 + (((v1 - v0) * f) >> x)`. Sub-cell slopes are strictly monotonic.
   * **COMPLETE in `libnoise`**: Added Ken Perlin's $C^2$ continuous quintic Hermite polynomial (`noise_wfade5`) and wired `NOISE_FADE5` into `noise_wcell`, `noise_wrow`, `noise_wplane`, and `noise_wpoint_*`.
   * **COMPLETE in `libchaff`**: Added Golden Ratio rotational skew (`chaff_rotate_q15`, $\theta \approx 31.7^\circ$) and `chaff_sample_rugged_2d()`.
   * **ACTIVE INTEGRATION**: Connecting `chaff_rotate_q15`, domain warping, and `NOISE_FADE5` into `nd_field_sample()` in `external/nd-world/include/nd/field.h` and `chaff_eval_multi()` in `<ttypt/chaff_glsl.h>` to break active terrain lattice resonance.

3. **Phase 3: Sub-Pixel Q12.4 Precision & Chunk Generation Overhaul (ACTIVE)**:
   * **COMPLETE in `field.h`**: Added `elevation_q12_4` to `nd_field_sample_t` and `nd_field_elevation_to_block()` for whole-block physics compatibility.
   * **ACTIVE INTEGRATION**: Storing Q12.4 fixed-point elevation into `chunk->height` in `mods/nd/ux/nd_seven_ground.c` and unpacking in WebGL2 `VS_TERRAIN_3D` (`unpackScale = 0.0625`).
   * **COMPLETE**: Eliminated legacy 2D CPU hillshade emboss filter in `nd_seven_ground.c:chunk_get_or_create()`, delivering pure unshaded diffuse albedo to 2D and 3D.

4. **Phase 4: WebGL2 Bilinear Height & Analytical Normals (COMPLETE)**:
   * **COMPLETE in `lamina-bridge.js`**: Upgraded `VS_TERRAIN_3D` with `sampleHeightSmooth`, using branchless 4-tap `texelFetch` bilinear height blending and Hermite sub-texel weights.
   * **COMPLETE in `lamina-bridge.js`**: Derived analytical surface normals directly from bilinear patch partial derivatives, eliminating normal spikes and terracing ridges.

5. **Phase 5: Multi-Scale Water & Optical Bathymetry (NEXT)**:
   * Wire `chaff_glsl` into `FS_WATER_3D` in `lamina-bridge.js` to replace placeholder diagonal sine wave.
   * Implement Beer-Lambert multi-scale optical depth attenuation ($I = I_0 e^{-\mu d}$) and swash/backwash shoreline foam dynamics ($d \le 2$).

6. **Phase 6: Stratosphere Camera & Horizon Curvature (COMPLETE)**:
   * Implemented curved planetary torus frame (`nd_seven_gl_torus_frame`), double-precision relative chunk origins, Rayleigh limb scattering, and ocean sun glint specular reflection.

7. **Phase 7: 4D Spatiotemporal Analytical Noise & Wind-Coupled Volumetric Clouds (COMPLETE)**:
   * Extended `libchaff` (`<ttypt/noise.h>`, `<ttypt/chaff.h>`, `<ttypt/chaff_glsl.h>`) with `chaff_sample_4d_t`, `chaff_sample_grad_4d()`, `chaff_fill_slice_4d()` ($16 \to 8 \to 4$ hypercube pre-collapse), and 8-byte `XXH32` `chaff_hash4`.
   * Integrated geostrophic wind-shear volumetric clouds (`s_vs_cloud`, `s_fs_cloud`, `nd_seven_gl_flush_clouds`), projected ground/water cloud shadows, and water cloud reflections in `mods/nd/ux/nd_seven_gl.c`. See [`docs/VOLUMETRIC_CLOUDS_AND_4D_NOISE.md`](VOLUMETRIC_CLOUDS_AND_4D_NOISE.md).
