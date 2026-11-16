# NeverDark Seven: Authoritative Water Requirements & Physical Specification

**Document**: Canonical Water Specification  
**Status**: Authoritative Reference (`WATER_REQUIREMENTS.md`)  
**Scope**: `external/liblamina` (`lamina-bridge.js`), NeverDark Seven (`mods/nd/ux`), `external/libnoise` (`libchaff`)

---

## 1. Executive Summary & Core Mandate

NeverDark Seven features a hybrid 2.5D diorama open-world rendering engine. Water simulation must achieve **photographic realism** and **rock-solid 60 FPS / 120 FPS performance** by adhering to the laws of physical optics and radiative transfer.

---

## 2. Requirements Specification

### R1. True View-Dependent Volumetric Optical Depth ("Ocular Esgotation")
* **Slant Path Length Across the 3D Water Column**:
  Extinction must be genuinely volumetric, not a flat 2D multiplier based on vertical height difference $\Delta z$. It must account for the actual optical ray path length $L$ through the fluid along the line of sight:
  $$L = \frac{d}{\cos \theta_{\text{view}}} = \frac{d}{\max(0.25, \mathbf{v}_{\text{view}} \cdot \mathbf{n}_{\text{up}})}$$
  Under Seven's 55° diorama pitch angle, viewing rays penetrate through $\approx 1.6\times$ more fluid volume before reaching the seabed.
* **Radiative Transfer: Spectral Absorption + Volumetric In-Scattering**:
  - **Transmitted Seabed Light**: Light reflected from the illuminated sea floor attenuates according to the Beer-Lambert law:
    $$\mathbf{C}_{\text{transmitted}} = \mathbf{C}_{\text{seabed}} \cdot \exp(-\mathbf{\mu} \cdot L)$$
    where $\mathbf{\mu} = (0.22, 0.07, 0.015)$ ensures red light is absorbed within the first few meters while green and blue/cyan penetrate.
  - **Volumetric In-Scattering**: Ambient light scattering off water molecules accumulates deep oceanic blue photons through the fluid volume:
    $$\mathbf{C}_{\text{inscatter}} = \mathbf{C}_{\text{ocean}} \cdot \left(1.0 - \exp(-\mathbf{\sigma} \cdot L)\right)$$
    where $\mathbf{C}_{\text{ocean}} = (0.012, 0.08, 0.30)$ provides authentic midnight sapphire depth.
  - **Fluid Body Composite**: $\mathbf{C}_{\text{body}} = \mathbf{C}_{\text{transmitted}} + \mathbf{C}_{\text{inscatter}}$.

---

### R2. Photo-Realistic Multi-Scale Wave Hierarchy (Lower & Higher Octaves)
Water waves must not look like coarse, single-scale flat bumps. The surface combines a 3-tier octave cascade:
1. **Lower-Frequency Macro Swell (`shift 6u`, 64-unit wavelength)**:
   - Broad, rolling oceanic ground swells providing physical wave travel along the prevailing wind direction.
2. **Mid-Frequency Wind Chop (`shift 4u`, 16-unit wavelength)**:
   - Active travelling waves decorrelated via Golden-Ratio rotational skew (`chaff_rotate_golden`, $\theta \approx 31.7^\circ$) to eliminate Cartesian lattice resonance and prevent repeating patterns across infinite distances:
     $$\begin{pmatrix} x' \\ y' \end{pmatrix} = \begin{pmatrix} 0.8506508 \cdot x - 0.5257311 \cdot y \\ 0.5257311 \cdot x + 0.8506508 \cdot y \end{pmatrix}$$
3. **Higher-Frequency Capillary Ripples (`shift 2u`, 4-unit wavelength)**:
   - Fine surface tension wavelets that catch sunlight glints and photographic micro-facets.
4. **Trochoidal Wave Shaping**:
   - Wave crests sharpen into peaked narrow ridges while wave troughs flatten and broaden out.
5. **Physical Surface Normal Derivation**:
   - Surface normals $\mathbf{N} = \text{normalize}(-\nabla h_x, -\nabla h_y, 1.0)$ are derived from the combined multi-scale wave gradient.

---

### R3. Realistic Caustics Cast Strictly on the Sea Floor
* **Physical Location**:
  - Caustics must **never** float on top of deep ocean water or be painted on the water surface.
  - Caustics are sunlight focused through surface wavelets projecting directly **onto the sea floor** (the seabed substrate).
* **Distance-Dependent Extinction**:
  - Direct sunlight collimation diffuses into ambient glow as depth increases: $\text{causticCoherence} = \exp(-0.45 \cdot d)$.
  - *Shallow water ($d \le 3$)*: Sharp, thin, dancing ribbons illuminating sand and rock textures with high contrast.
  - *Mid-depth ($d = 4 \dots 6$)*: Caustics soften into delicate fluid shimmer.
  - *Deep water ($d > 7$)*: Direct sunlight is completely diffused and absorbed; **caustics are 100% extinguished**.
* **Contrast Formulation**:
  - Preserves the exact 3D dual-octave interference formula from commit `7add395`:
    $$\text{diff} = |c_1 - c_2|, \quad \text{caustic} = \text{pow}(\text{clamp}(1.0 - \text{diff} \cdot 6.0, 0.0, 1.0), 3.0)$$

---

### R4. Decoupling: Never Tint the Water Surface with Terrain Depth in 3D
* In 2D legacy software mode, depth tinting blends directly into pixels because no 3D geometry exists.
* In **3D WebGL mode**:
  - The water surface is an independent horizontal mesh interface at $Z = \text{SeaLevel}$.
  - The terrain mesh is the physical sea floor at $Z = \text{SeabedHeight}$.
  - The 3D water surface interface must **never** be tinted with terrain depth. The surface interface strictly handles:
    1. Multi-scale wave relief and slope motion.
    2. Directional sun lighting from the Northwest sun vector ($\mathbf{N} \cdot \mathbf{L}_{\text{sun}}$).
    3. Specular sun sparkle ($\mathbf{N} \cdot \mathbf{H}^{36}$).
    4. Fresnel sky reflections ($R \approx F_0 + (1 - F_0)(1 - \mathbf{N} \cdot \mathbf{V})^4$).
  - All depth-dependent coloration lives strictly in the **water column and sea floor underneath**.

---

### R5. High Performance & 60 FPS / 120 FPS Fluid Dynamics
* Fragment shader instruction counts must remain lean and optimized.
* Retained GPU chunk texture caching avoids synchronous multi-megabyte per-frame texture uploads.
* 3D mode completely bypasses CPU software framebuffer clears and blits.

---

### R6. Architectural & Boundary Invariants
1. **Server Zero-OpenGL**: The server (`nd.so`) runs strictly as pure software C99 integer math with zero GPU/GL dependencies; SSR continues to function seamlessly.
2. **No Site-Specific JavaScript**: `lamina-bridge.js` remains pure generic graphics infrastructure without domain terms (`song`, `gig`, `poem`, `seven`).
3. **Clean Shader Contracts**: 100% clean GLSL ES 3.00 compilation with zero undeclared variables, verified by automated tests in `tests/unit/test-lamina-bridge-routing.mjs`.
