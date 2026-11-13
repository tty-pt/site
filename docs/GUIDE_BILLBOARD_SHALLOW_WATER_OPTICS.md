# Engineering Guide: Calibrating Shallow Water Optical Extinction for Billboards

## 1. Executive Summary & Scope Boundary
- **Mob Buoyancy Scope Exclusion (R1)**: Complex floating behaviors for general mobs (dependent on creature species, mass, swim animations, and aquatic AI states) are explicitly out of scope for the current rendering pass. Living mob entities remain grounded on terrain seabed/soil until dedicated entity simulation quests introduce swimming physics.
- **Objective (R2)**: Provide a comprehensive technical blueprint for making optical extinction significantly less pronounced in shallow water so that shallows appear crystal-clear and radiant, while ensuring partial and entire submersion remain unmistakably visible through physical optical boundary cues.

---

## 2. Theoretical Optics & Root Cause Diagnosis
In the NeverDark Seven diorama pipeline (`external/liblamina/htdocs/lamina-bridge.js:330-345`), billboard fragment shading evaluates physical Beer-Lambert spectral absorption:
$$\mathbf{C}_{\text{submerged}} = (\mathbf{C}_{\text{sprite}} + \mathbf{C}_{\text{caustic}}) \odot e^{-\mathbf{\mu} \cdot \ell} + \mathbf{C}_{\text{scattered}}$$
Where:
- $d = \text{uSeaLevel} - \text{vWorldPos.z}$ is fragment submersion depth.
- $\theta \approx 55^\circ$ is the camera diorama pitch angle.
- $\ell = d / \cos\theta$ is the optical path length traversed through the water column.

### Why Shallow Water Still Looked "Too Deep"
1. **Linear Optical Path from Depth Zero**: Beer-Lambert exponential decay begins attenuating red light immediately at $d = 0.1$, absorbing up to 40% of red light in the top 10-15 units of water.
2. **Immediate In-Scattering Accumulation**: Deep ocean in-scattering was evaluated uniformly without a shallow-water deadband or non-linear ramp, adding a persistent blue haze even to fragments submerged by only 2-4 units.
3. **Contrast Misalignment**: In real shallow coastal water (0-1 meter depth), water is 95%+ transparent; the dominant visual cues indicating submersion are:
   - The surface meniscus refraction ring at the waterline boundary.
   - High-contrast, dancing solar caustics on the submerged surfaces.
   - Surface wave disturbance ripples.

---

## 3. Submersion Perception Invariants
Even when water is 95%+ clear in the shallows, submersion must remain unmistakably visible. To achieve this without relying on heavy blue fog:
1. **Waterline Meniscus Highlight**: A sharp, radiant refraction/reflection ring right where the water surface intersects the billboard ($d \approx 0.0 .. 0.8$). This provides an unmistakable horizontal boundary marker between the dry upper body and wet lower body.
2. **High-Contrast Surface Caustics**: Sun rays refracted by surface waves cast dancing caustic bands across the submerged sprite surface (`submergedCaustic`). In shallow water, caustics are at their most coherent and brilliant, fading with depth.
3. **Smooth Non-Linear Extinction**: Use a smooth depth deadband (`smoothstep(0.0, 16.0, d)`) so that 0-8 units of depth remain clear, transitioning smoothly into deep ocean absorption for depths $> 16$.

---

## 4. 3D WebGL Shader Implementation Blueprint (R3)
- **Target File**: `external/liblamina/htdocs/lamina-bridge.js`
- **Target Shader**: `FS_BILLBOARD_3D` (~line 330)

### Exact Mathematical Formulation:
```glsl
if (uSeaLevel > -900.0 && vWorldPos.z < uSeaLevel) {
    float d = uSeaLevel - vWorldPos.z;

    // 1. High-Contrast Solar Caustics (peaks at shallow depths)
    float c1 = chaff_sample_3d(vec3(vWorldPos.xy * 2.0, uAnimTime * 3.5), 3u, 0x1337u);
    float c2 = chaff_sample_3d(vec3((vWorldPos.xy + vec2(37.0, -23.0)) * 2.0, uAnimTime * 2.5), 3u, 0xBEEFu);
    float diff = abs(c1 - c2);
    float caustic = pow(clamp(1.0 - diff * 6.0, 0.0, 1.0), 3.0);
    float causticCoherence = exp(-0.12 * d);
    vec3 submergedCaustic = vec3(0.50, 0.85, 1.0) * (caustic * 0.45 * causticCoherence);

    // 2. Dual-Regime Non-Linear Shallow Optical Path
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    float cosView = max(0.25, dot(vec3(0.0, 0.0, 1.0), viewDir));
    float shallowFactor = smoothstep(0.0, 16.0, d);
    float effectiveOpticalPath = (d * (0.25 + 0.75 * shallowFactor)) / cosView;

    // Spectral absorption: gentle in shallows, deep extinction at depth
    vec3 waterAbsorption = exp(-vec3(0.020, 0.008, 0.002) * effectiveOpticalPath);
    vec3 waterColor = (lit + submergedCaustic) * waterAbsorption;

    // Volumetric deep ocean in-scattering (suppressed in top 6 units)
    vec3 deepOceanScattered = vec3(0.005, 0.035, 0.12) * shallowFactor * 
                              (1.0 - exp(-vec3(0.010, 0.018, 0.030) * effectiveOpticalPath));
    lit = waterColor + deepOceanScattered;

    // 3. Crisp Waterline Meniscus Highlight
    float meniscus = smoothstep(0.8, 0.0, d);
    vec3 meniscusColor = vec3(0.95, 0.98, 1.0);
    lit = mix(lit, meniscusColor, meniscus * 0.60);
}
```

---

## 5. Preserving Partial & Complete Submersion Perception (R4)
To ensure the observer clearly perceives when a billboard is partially or completely underwater without excessive color extinction:
1. **Partial Submersion**: The meniscus highlight acts as a sharp boundary marker across the billboard quad. The dry portion ($vWorldPos.z \ge uSeaLevel$) reflects standard ambient light, while the wet portion immediately below the meniscus exhibits moving caustics and subtle refraction.
2. **Complete Submersion**: As depth exceeds 16 units, `shallowFactor` approaches `1.0`, and full Beer-Lambert spectral absorption attenuates red light while `deepOceanScattered` introduces deep bathymetric blue, rendering deeply submerged objects authentically obscured.

---

## 6. 2D Software Fallback Implementation Blueprint (R3, R4)
- **Target File**: `mods/nd/ux/nd_seven_atlas_draw.c`
- **Target Function**: `blit_sub` (~line 119)

### Exact Implementation:
```c
if (y >= y_water) {
    int d_row = y - y_water;
    if (d_row == 0) {
        /* Crisp waterline meniscus highlight */
        c = alpha_blend_rgb(c, RGB(240, 250, 255), 160);
    } else {
        uint32_t wcol = water_color_at_depth(d_row, 128, 128, 128);
        int alpha = water_alpha_at_depth(d_row);
        /* Non-linear shallow alpha ramp for crystal-clear shallows */
        int shallow_alpha = (d_row < 6) ? (alpha * d_row / 12) : (alpha * 2 / 5);
        c = alpha_blend_rgb(c, wcol, shallow_alpha);
    }
}
```

---

## 7. Verification Checklist & Regression Prevention
1. **Dual-Compile WASM**: Verify both native `.so` and browser `.wasm` compile with zero errors:
   `make -C mods/nd && make site-wasm`
2. **Shader Routing Unit Test**:
   `node tests/unit/test-lamina-bridge-routing.mjs`
3. **C Render Suite**:
   `bash tests/unit/run-test-seven-render.sh`
4. **WASM Package Test**:
   `npm --prefix external/liblamina/npm test`

---

## 8. Step-by-Step Implementation Protocol for the Next Agent (R5)
When a future engineering quest implements this calibration:
1. **Inspect Shader Baseline**:
   - Open `external/liblamina/htdocs/lamina-bridge.js`.
   - Locate `FS_BILLBOARD_3D` (~line 330).
   - Substitute the linear `opticalPath` with the dual-regime `shallowFactor = smoothstep(0.0, 16.0, d)` and `effectiveOpticalPath = (d * (0.25 + 0.75 * shallowFactor)) / cosView`.
   - Update `waterAbsorption` coefficients to `vec3(0.020, 0.008, 0.002)`.
   - Modulate `deepOceanScattered` by `shallowFactor`.
   - Boost `meniscus` to `smoothstep(0.8, 0.0, d) * 0.60`.
2. **Inspect 2D Blit Baseline**:
   - Open `mods/nd/ux/nd_seven_atlas_draw.c`.
   - Locate `blit_sub` (~line 119).
   - Scale shallow row alpha using `(d_row < 6) ? (alpha * d_row / 12) : (alpha * 2 / 5)`.
3. **Test Suite Execution**:
   - Run `npm --prefix external/liblamina/npm test`.
   - Run `node tests/unit/test-lamina-bridge-routing.mjs`.
   - Run `bash tests/unit/run-test-seven-render.sh`.
   - Run `sh scripts/check-module-boundaries.sh && sh scripts/check-wasm-imports.sh`.
4. **Success Criteria**:
   - In 3D WebGL mode, a lamb half-submerged in shallow water retains its crisp white fur color with zero excessive blue wash, while the waterline boundary is sharply delineated by the white meniscus highlight and moving solar wave caustics.
   - In 2D CPU fallback mode, the waterline row exhibits a clear surface highlight, with subtle, non-intrusive water blending below.


