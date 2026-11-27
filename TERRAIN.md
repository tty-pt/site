# NeverDark Terrain — Realism Audit & Next-Step Observations

Status snapshot after landing canonical Earth-like tectonic orogeny (`ND_FIELD_OROGENY_SALT`) and altitude-coupled alpine crag relief in [`external/nd-world/include/nd/field.h`](external/nd-world/include/nd/field.h) and [`external/nd-world/include/nd/field_glsl.h`](external/nd-world/include/nd/field_glsl.h).

> **Execution Roadmap:** See [`docs/PHOTOREAL_TERRAIN_ROADMAP.md`](docs/PHOTOREAL_TERRAIN_ROADMAP.md) for the active 9-phase plan (Phases 0–8) grounding all landforms in 1:1 physical metric scale (`45.0 wu/m`, lamb = `40 cm`) and scalable planet geometry (`planet_bits = 16..23`).

---

## Core Diagnosis

NeverDark already has strong foundational systems (continuous ground-to-orbit torus, physical Beer-Lambert water optics, volumetric 3D clouds, 12 ground regimes, dipping rock strata, snowline/glaciers/alpenglow, and now tectonic mountain chains up to `1854 wu`).

Even with tectonic orogeny added, however, the world still reads as **impressive procedural terrain** rather than **a real Earth-like world**. Six concrete gaps explain why.

---

## 1. Horizon Presence, Relief Scale & Regional Variation (Primary User Observation)

> *"Even in regards to mountains still it doesn't look like I have the terrain variation I would expect from the real world. I don't see big mountains on the horizon nor anything like that, like I would imagine."*

Why big mountains still fail to loom dramatically on the horizon from ground level:

- **Aspect ratio / angular elevation**: Current orogeny spans wide horizontal wavelengths (`640..41,000 wu`, i.e. 4..256 rooms) while peaking around `~1,500–1,600 wu` above sea level (and capped at `1854 wu`). A `1,500 wu` peak `10,000–24,000 wu` away subtends only `~3.5°–8.5°` above the local tangent plane—further compressed by planetary torus curvature (`d² / 2R`), distant valley fog, and downward-angled default camera pitch.
- **Spawn & regional locality**: World 0 spawn sits in a calm lowland/plain, and ~50% of land is plains. Walking around locally doesn't reliably frame dramatic mountain silhouettes, foothills, escarpments, or bold landforms within typical horizon sightlines unless you travel far to hit a tectonic belt.
- **Uniform statistical rhythm**: Macro variation is still governed by one smooth, stationary blend of base height + orogeny mask, rather than abrupt real-world contrasts (e.g., flat coastal plain meeting a sudden wall of foothills and jagged alpine spires, or a wide river valley framed by steep canyon walls / ridge silhouettes).

**Directions to investigate**:
- Evaluate vertical ceiling & scale (`1854 wu` cap vs steeper local ridge prominence / higher peak-to-width aspect ratios, sharper massif walls, and visible piedmont/cordillera framing from lowland basins).
- Check camera horizon framing, valley fog density vs peak silhouette contrast, and LOD attenuation at mid-to-far range (ensuring distant mountain peaks don't lose height/sharpness in macro quadtree mip/coarse LODs).
- Introduce true **meso-scale geological provinces** (dramatic escarpments, alpine massifs, canyonlands/mesas, rolling hills, flat basins, coastal cliffs) so adjacent regions visibly differ in character rather than averaging into the same slope spectrum.

---

## 2. Mountains Look *Extruded*, Not *Eroded*

Ridged multifractal produces crests and peaks, but real mountains are sculpted by **water and ice tearing them down**, not just tectonics pushing them up:

- Slopes lack branching dendritic gullies, glacial cirques, sharp arêtes, V-shaped gorges, and radiating alluvial fans / talus aprons.
- **Hydrology is decoupled from mountains**: Rivers in `external/nd-world/include/nd/field.h` (`nd_field_eval_river_ex`) currently cap at `+290 wu` above sea level and follow independent zero-crossing noise rather than draining *off* the `1,000–1,600+ wu` ranges. Mountains do not feed upland streams, carve valley chutes, or connect hydrologically into lowland rivers and deltas.

---

## 3. No Long-Range Mountain Cast Shadows

- `s_fs_terrain_shadow` in `mods/nd/seven/nd_seven_gl_shaders.c` writes terrain into the **local tree shadow map** (`uTreeShadowWriteOrigin`), which only covers a tight radius around the player.
- A `1,500 wu` mountain range several kilometers away casts **no shadow** across valley floors, foothills, or neighboring ridges—especially noticeable at sunrise, sunset, and mid-afternoon.
- In reality, long topographic cast shadows + cool blue sky-lit shadow faces vs. warm sunlit slopes + layered blue aerial perspective between ridge planes are what give mountains unmistakable mass and monumental scale.

---

## 4. Vertical Zonation & Cloud Ceiling Mismatch with Taller Mountains

Several altitude thresholds were originally calibrated for the pre-orogeny ~600 wu height ceiling and are now out of proportion with `1,600+ wu` peaks:

- **Snowline starts too low**: `mods/nd/seven/nd_seven.c` (substrate color `320..620 wu`) and `mods/nd/seven/nd_seven_gl_shaders.c` (`snowBaseZ ~ 280 wu`, full snow by `~460 wu`) turn vast lower mountain flanks uniformly white instead of showing the Earth altitude ladder: *forested foothills -> crisp timberline -> alpine meadow/tundra -> dark exposed granite/slate crags -> high glaciers & summit snow*.
- **Cloud-shadow slice vs. mountain height**: The cloud-shadow projection slice in `mods/nd/seven/nd_seven_gl_shaders.c` sits around `zk = 920 wu`, below the tallest summits (`~1,600+ wu`). Peaks poking above `zk` flip the sign of `dz` in `eval_planetary_cloud_shadow` / `nd_cloud_shadow_map` rather than interacting cleanly with cloud bases or forming orographic summit caps.

---

## 5. Mid-Distance Scale Cues (The "Toy Model" Trap)

What tells the human eye a peak is **3,000 meters tall** instead of a **30-meter grassy hill**:

- **Distant forest massing & treeline**: Up close, 3D instanced trees provide strong scale; past tree draw distance, forested slopes fall back to smooth green shader skin. On Earth, distant foothills are carpeted in textured dark-green canopy massing that terminates along a sharp, climate- and slope-driven treeline.
- **Crisp rock ribs piercing snowfields**: Steep alpine faces need sharp, dark, strata-banded rock buttresses cutting cleanly through snow rather than soft smoothstep color transitions.
- **Ridge-by-ridge atmospheric separation**: Distinct aerial perspective bands separating foreground hills, mid-ground ranges, and distant horizon peaks.

---

## 6. Climate Coupling & Signature Landforms

- **No orographic rain shadows**: Prevailing winds hitting mountain ranges do not yet create lush green windward slopes and arid ochre leeward basins (Andes / Cascades / Himalayas effect)—one of the strongest visual cues of a real planet from both ground and orbit.
- **No distinct macro geological personalities**: Stationary noise makes coastlines, plains, and highlands share a uniform statistical texture. Real Earth gets its memorability from distinct landform regimes: fjords, coastal sea cliffs vs. wide sandy bays, glacial U-valleys, flat-topped mesas/canyons, braided river deltas, and volcanic calderas.
