# NeverDark Photorealistic Real-World Terrain Roadmap (1:1 Physical Scale & Scalable Planets)

Authoritative engineering plan for transforming NeverDark's procedural terrain from extruded/toy-scale noise into **photograph-grade real-world landscapes** across all terrestrial landforms, grounded in **1:1 physical metric calibration** (`45.0 wu/m`) and engineered from day one so **planet size scales cleanly** from `planet_bits = 16` up to Earth-scale (`planet_bits = 22..23`).

> **Companion documents:** [TERRAIN.md](../TERRAIN.md) (initial realism audit), [docs/WORLD_MATH.md](WORLD_MATH.md) (orbital/climate math), [docs/TERRAIN_ROADMAP.md](TERRAIN_ROADMAP.md) (prior 2.5D/cavern milestones), [docs/WATER_REQUIREMENTS.md](WATER_REQUIREMENTS.md), [docs/OCCLUSION.md](OCCLUSION.md).

---

## Live Phase Status Tracker

| Phase | Title | Status | Gate / Notes |
|-------|-------|--------|--------------|
| **Phase 0** | Metric Scale (`45 wu/m`), `nd_planet_scale_t`, `int32_t` Elevations & Chunk-Relative `RGBA16F` | **COMPLETE** | Bit-exact at `b = 16` (`worldgen_golden.txt` 1280 rows), Everest `398,250 wu` fp16 err `<= 0.25 wu` (`< 5.6 mm`), `b = 16..23` verified |
| **Phase 1** | World-Scale Horizon Shadow Maps & Mountain Shadow Relief | **COMPLETE** | 2-cascade `256×128` `GL_RGBA16F` horizon shadow + 8-azimuth sky-AO atlas (`uTerrHorizonMap`, `NdFrameUBO` `320 B`), coupled across all 5 scene pipelines; Everest fp16 `< 2.5 m`, 0 steady-state GL uploads |
| **Phase 2** | Geological Province Synthesis & Metric Hypsometry (10 Landform Archetypes) | Planned | Flat plains + dramatic escarpments + 1,800 m alpine cordilleras at `b = 16` |
| **Phase 3** | Erosion-Sculpted Mountains & Coupled Mountain-to-Sea Hydrology | Planned | Ridge-to-valley drainage, alluvial aprons, cirques, braided deltas |
| **Phase 4** | Climate-Coupled Altitude Zonation, Orographic Rain Shadows & Volumetric Cloud Ceiling | Planned | Metric timberline/snowline, windward/leeward contrast, summits piercing clouds |
| **Phase 5** | Photorealistic Micro-Relief, Strata-Ribbed Crags & Distant Canopy Massing | Planned | Anti-toy-model mid-range detail, rocky arêtes piercing snow, distant canopy texture |
| **Phase 6** | Coastline Personality, Cliff/Bay Contrast & Glacial/Fluvial Landform Accents | Planned | Sea cliffs, sandy coves, fjords, estuaries, talus fans |
| **Phase 7** | Camera Horizon Framing, Ridge-by-Ridge Aerial Perspective & Atmospheric Depth | Planned | Photographic telephoto/wide horizon framing, layered blue-haze depth separation |
| **Phase 8** | Planet Scaling Activation (`planet_bits = 18..23`) & Server/World Coord Migration | Planned | Opt-in larger planets (`axil-nd` `int32_t` room coords, `b = 18..23`) |

---

## 1. Fundamental Metric Anchor (`45.0 wu/m`)

All physical measurements are anchored to the canonical standing lamb mesh in [`external/nd-plant/include/nd/plant_mesh3d.h`](../external/nd-plant/include/nd/plant_mesh3d.h):

- **Standing lamb height:** `ND_MESH3D_LAMB_HEIGHT_M = 0.40 m` (`40 cm`)
- **Standing lamb body height in world units:** `ND_MESH3D_LAMB_BODY_WU = 18.0 wu`
- **Canonical scale constant:** `ND_FIELD_WU_PER_METER = 45.0 wu/m`
- **Derived units:**
  - `1 wu = 2.222 cm` (`0.02222 m`)
  - `1 m = 45 wu`
  - `1 km = 45,000 wu`
  - `1 room (160 × 128 wu) = 3.556 m × 2.844 m` (`10.11 m²`)
  - `1 L0 chunk (640 × 512 wu = 4 × 4 rooms) = 14.22 m × 11.38 m`
  - Sub-pixel Q27.4 elevation (`1/16 wu`) = `1.39 mm` vertical resolution

### Why Previous Terrain Looked Toy-Scaled
Before Phase 0, the world ceiling was `ND_FIELD_MAX_ELEV_WU = 6,400 wu` (actual orogeny peaks `~1,600–1,854 wu`). At `45 wu/m`:
- A `1,600 wu` "alpine peak" was only **`35.6 meters` tall** (a 10-story apartment building!).
- A `280 wu` snowline started at **`6.2 meters`** above sea level (taller than two lambs stacked on a pickup truck).
- A `920 wu` cloud shadow deck sat at **`20.4 meters`** above sea level (barely clearing oak treetops).
- A `10,000 wu` distant horizon mountain sat only **`222 meters`** away.

---

## 2. Scalable Planet Geometry (`planet_bits = 16..23`)

Default gameplay planets stay at **`planet_bits = 16`** throughout Phases 0–7 so existing `axil-nd` server worlds (`int16_t` room coordinates) maintain 100% backwards compatibility with zero breaking changes.

Every subsystem (`nd-world`, `libareole`, `nd_seven`, shaders) derives its world dimensions, relief ceiling, and torus curvature from **`nd_planet_scale_t`** computed by `nd_planet_scale_for_bits(bits, room_w, room_h)`:

| `planet_bits` | Rooms/Side (`2^b`) | Flat Span (`km × km`) | Torus `R` (`km`) | Torus `r0` (`km`) | Torus Area (`km²`) | Real-World Equivalent | Target `max_relief_m` (`wu`) | `areole max_levels` |
|---:|---:|---:|---:|---:|---:|---|---:|---:|
| **16** *(default)* | `65,536` | `233 × 186 km` | `44.4 km` | `29.7 km` | `52,053 km²` | Switzerland / Costa Rica | `1,800 m` (`81,000 wu`) | `17` |
| **18** | `262,144` | `932 × 746 km` | `177.8 km` | `118.7 km` | `832,850 km²` | France / Texas / Iberia | `4,500 m` (`202,500 wu`) | `19` |
| **20** | `1,048,576` | `3,728 × 2,983 km` | `711.1 km` | `474.7 km` | `13,325,600 km²` | Europe / Australia | `6,000 m` (`270,000 wu`) | `21` |
| **22** | `4,194,304` | `14,913 × 11,930 km` | `2,844.4 km` | `1,898.7 km` | `213,209,600 km²` | ~42% Earth surface | `8,850 m` (`398,250 wu`) | `23` |
| **23** | `8,388,608` | `29,826 × 23,861 km` | `5,688.9 km` | `3,797.3 km` | `852,838,400 km²` | Earth+ super-planet | `8,850 m` (`398,250 wu`) | `24` |

### Core Scaling Law: Fixed Physical Wavelengths
Geomorphic features (foothill ridges, canyon walls, cirques, valleys, erosion channels, micro-crags) are defined in **physical meters**, *not* fractions of planet circumference:
- When `planet_bits` increases from `16 → 18 → 20 → 22`, hills and peaks do **not** stretch wider into soft blobs.
- Instead, larger planets unlock additional **macro tectonic octaves** (wider oceans, longer cordilleras, higher continental interior plateaus, taller summits up to Everest `8,850 m`).

---

## 3. Core Data Layouts & Precision Invariants (Phase 0 Foundation)

### 3.1 `nd_planet_scale_t` (`external/nd-world/include/nd/field.h`)
Single source of truth for planet dimensions, torus radii, physical height ceilings, and atmospheric/climate altitude bands:
```c
typedef struct {
    uint8_t  planet_bits;        /* log2(rooms per axis): 16 default, up to 23 */
    uint8_t  areole_max_levels;  /* planet_bits + 1 (17 at b=16, 24 at b=23) */
    uint8_t  _pad[2];
    uint32_t rooms_per_axis;     /* 1u << planet_bits */
    int64_t  world_w_wu;         /* rooms_per_axis * room_w */
    int64_t  world_h_wu;         /* rooms_per_axis * room_h */
    float    wu_per_meter;       /* 45.0f (lamb 18 wu = 0.40 m) */
    float    max_relief_m;       /* target max mountain relief in meters */
    int32_t  max_elev_wu;        /* int32_t elevation cap in wu */
    int32_t  min_elev_wu;        /* int32_t bathymetric trench floor in wu */
    float    torus_R_wu;         /* major radius scaled with planet_bits */
    float    torus_r0_wu;        /* minor radius scaled with planet_bits */
    float    snowline_base_wu;   /* metric snowline base (equatorial/temperate ref) */
    float    cloud_lcl_wu;       /* metric cumulus lifting condensation level */
} nd_planet_scale_t;
```

### 3.2 Elevation Representation (`int32_t` & `Q27.4`)
- **Problem:** Prior `int16_t` elevations capped at `±32,767 wu` (`±728 m`), making a `1,800 m` Swiss alpine peak (`81,000 wu`) or `8,850 m` Everest peak (`398,250 wu`) physically impossible.
- **Invariant:** All internal and sampled elevations in `nd_field_sample_t`, `nd_field_room_memo_t`, `nd_field_bimodal_hypsometry`, `nd_field_orogeny_wu*`, and `nd_field_room_hyp_coarse*` use **`int32_t`**.
- **Sub-pixel fixed point (`elevation_q12_4` / `Q27.4`):** `int32_t` with 4 fractional bits supports `±134,217,727 wu` (`±2,982 km`) at `1.39 mm` precision.
- **Note on `libareole` (`ar_config_t.period_x/y`):** `int32_t` holds up to `b = 22` (`2^22 × 160 = 671,088,640 < 2.147B`); `b >= 23` (`2^23 × 160 = 1.34B`, `b = 24` overflows `int32_t`) will widen `ar_config_t` period fields to `int64_t` in Phase 8.

### 3.3 Chunk-Relative `GL_RGBA16F` Heightmaps (`mods/nd/seven/`)
- **Problem:** IEEE-754 `fp16` max finite value is `65,504` (`1,455 m`), and above `32,768 wu` its ULP degrades to `32 wu` (`71 cm` stair-stepping!).
- **Solution:** Store heights in `uHeightTex` **relative to a per-chunk integer baseline `base_z_wu`**:
  - `base_z_wu = floorf(0.5f * (chunk_min_z_wu + chunk_max_z_wu) + 0.5f)`
  - Texel `.r = (float)height_q4 * 0.0625f - base_z_wu`
  - Every shader reading `uHeightTex` adds `base_z_wu` back in `fp32` immediately after bilinear fetch.
- **Precision Achieved at Everest (`398,250 wu = 8,850 m`):**
  - L0 ground chunk (`~14 m` wide, vertical relief `< 15 m = 675 wu`): `|h - base_z| < 350 wu` → `fp16` ULP = **`0.25 wu = 5.5 mm`** (zero stair-stepping!).
  - Extreme cliff/macro chunk (vertical relief `3,000 wu = 67 m`): `|h - base_z| < 1,500 wu` → `fp16` ULP = **`1.0 wu = 2.2 cm`** (zero overflow, zero NaN/Inf!).
- **GPU Bindings (Zero Extra Draw/FFI Calls):**
  - **Terrain VS/FS:** `gl_chunk_inst_t` grows from `48 B` → **`64 B`** (exact 1 CPU cache line, 4 × `vec4`), adding `float height_z[4]` at attrib loc `4`: `{base_z_wu, min_z_wu, max_z_wu, 0.0f}`.
  - **Water VS/FS:** `uChunkLayers` widens `vec2` → `vec3` (`slot_layer, river_layer, base_z_wu`).
  - **Terrain Shadow VS:** `uChunkLayer` widens `float` → `vec2` (`slot_layer, base_z_wu`).
  - **Cloud FS:** `uChunkLayer` widens `float` → `vec2` (`slot_layer, base_z_wu`).
  - **`NdFrameUBO`:** grows from `272 B` → **`288 B`** (18 × `vec4`, exact `std140`), adding `vec4 uPlanetMetric` (`wu_per_meter, max_elev_wu, snowline_base_wu, cloud_lcl_wu`).

---

## 4. Target Real-World Landform Archetypes (Phases 2–6)

Terrain must stop averaging every coordinate into one stationary noise spectrum. Phase 2 introduces a deterministic, continuous geological province field (`nd_geology_t`) blending 10 distinct real-world landform archetypes:

1. **Alluvial Coastal Plains & Floodplains** — Near-flat (`0–2°` slopes), meandering rivers, subtle levees, expansive horizon sightlines.
2. **Rolling Piedmont & Pastoral Hills** — Soft convex-concave `5–18°` undulations (`30–180 m` local relief), oak/meadow mosaics.
3. **Alpine Folded Cordilleras** — Sharp arêtes, pyramidal peaks, glacial cirques, steep `35–60°` granite faces rising to `1,200–1,800 m` (at `b=16`, up to `8,850 m` at `b=22`).
4. **Canyonlands & Layered Mesas / Buttes** — Flat caprock plateaus carved by stepped stratified cliffs and deep V/slot canyons.
5. **Escarpments & Fault-Block Uplifts** — Asymmetric mountain fronts: abrupt steep scarp wall facing a flat basin on one side, gentle dip slope on the other (Sierra Nevada / Teton profile).
6. **Glacial U-Valleys & Fjords** — Broad flat-bottomed troughs bounded by towering near-vertical walls plunging straight into deep water.
7. **Volcanic Cones & Calderas** — Radial symmetry with breached craters, parasitic cones, and basaltic lava aprons.
8. **Karst & Limestone Towers / Sinkholes** — Steep fluted pinnacles and terraced humid valleys.
9. **Arid Basins, Playas & Dune Erg Fields** — Wide enclosed flat salt/sand basins flanked by bajadas and wind-sculpted barchan/star dunes.
10. **Dramatic Coastlines (Sea Cliffs vs. Barrier Beaches & Deltas)** — Hard-rock headlands dropping sharply into surf contrasting with gentle sweeping sandy coves and braided river estuaries.

---

## 5. Detailed Phase Breakdown (Phases 0–8)

### Phase 0 — Metric Scale (`45 wu/m`), `nd_planet_scale_t`, `int32_t` Elevations & Chunk-Relative `RGBA16F` *(Complete)*
- **Objective:** Remove all `int16_t` (`±32,767 wu`), `fp16` (`65,504 wu`), and hardcoded `b = 16` (`8,388,608 wu`) ceilings while preserving **100% bit-exact output** at `b = 16` (`worldgen_golden.txt` unchanged).
- **Deliverables:**
  1. `ND_FIELD_LAMB_HEIGHT_M`, `ND_FIELD_LAMB_BODY_WU`, `ND_FIELD_WU_PER_METER`, `nd_planet_scale_t`, `nd_planet_scale_for_bits()`, `nd_field_get_planet_scale()` / `nd_field_set_planet_bits()` in `external/nd-world/include/nd/field.h`.
  2. Widen internal/sample elevation path (`nd_field_sample_t`, `nd_field_room_memo_t`, `nd_field_bimodal_hypsometry`, `nd_field_orogeny_wu*`, `nd_field_room_hyp_coarse*`) to `int32_t`; wire dynamic `planet_bits` into periodic noise calls.
  3. Chunk-relative `RGBA16F` heightmap baking (`nd_seven_gl_bake_chunk_height_derived_rel`), `base_z_wu` in `gl_chunk_inst_t` (64 B), `uChunkLayers` (`vec3`), `uChunkLayer` (`vec2`), and `uPlanetMetric` in `NdFrameUBO` (288 B).
  4. Replace hardcoded `8388608.0` world-height divisors in `nd_seven_gl_shaders.c` and `nd_seven_gl_occ.c` with active `uWorldDim` / `nd_planet_scale_t`.
  5. Unit tests verifying `b = 16..23` scaling math, `int32_t` headroom up to `400,000 wu`, periodic continuity at `b = 18/20`, and `< 0.5 wu` (`< 1.1 cm`) fp16 reconstruction accuracy at Everest altitude (`398,000 wu`).

### Phase 1 — World-Scale Horizon Shadow Maps & Mountain Shadow Relief *(Complete)*
- **Objective:** Mountains cast crisp, accurate topographic shadows across kilometers of valley floors, foothills, and neighboring ridges at all sun elevations.
- **Deliverables:**
  1. Pure-C 2-cascade horizon shadow + 8-azimuth sky-hemisphere AO kernels (`nd_seven_horizon_ring_t`, `nd_seven_gl_horizon_windows`, `nd_seven_gl_horizon_sample_field`, `nd_seven_gl_horizon_compute_sky_vis`, `nd_seven_gl_horizon_sweep_sun`, `nd_seven_gl_horizon_pack_rgba`, `nd_seven_gl_horizon_eval_cpu`) in `mods/nd/seven/nd_seven_gl_math.c` & `nd_seven_gl.h`, with Far-to-Near border seeding and 8-texel world snapping.
  2. Single `256 × 128` `GL_RGBA16F` atlas (`uTerrHorizonMap`: left tile Near Cascade, right tile Far Cascade) allocated once and dirty-gated via `glTexSubImage2D` in `mods/nd/seven/nd_seven_gl_occ.c` (0 steady-state GL uploads/allocations; `NdFrameUBO` widened to `320 B` with `uTerrHoriz0`/`uTerrHoriz1`).
  3. Full fragment-shader coupling (`s_glsl_terr_horizon_fn`, 1-tap Near fast path, Near→Far crossfade, orbital fade, Rayleigh alpine blue `shadowCool`, `skyHemAO`, flat-plain invariance) across terrain (all 7 variants), water, mesh3d (alpha + opaque), grass, and billboards (`mods/nd/seven/nd_seven_gl_shaders.c`, `nd_seven_gl.c`, `nd_seven_gl_billboard.c`).
  4. Unit tests in `tests/unit/test_seven_render.c` verifying cascade scaling/snapping, flat-plain invariance, `1,800 m` ridge shadows across 6 cardinal+diagonal azimuths, Far-to-Near border seeding, 45° alpine bowl SVF, Everest `398,250 wu` `RGBA16F` packing (`< 2.5 m` error), and real `nd_field` alpine shadow relief.

### Phase 2 — Geological Province Synthesis & Metric Hypsometry (10 Landform Archetypes)
- **Objective:** Replace the uniform smooth noise blend with sharp regional geological personalities and true metric relief (up to `1,800 m` / `81,000 wu` at `b = 16`, scaling via `nd_planet_scale_t`).
- **Approach:** Add `nd_geology_t` (province ID, tectonic uplift, strata hardness, basin flatness, erosion age) evaluated deterministically in `nd-world` (CPU + `field_glsl.h`). Ensure lowlands are genuinely flat/rolling while cordilleras, escarpments, and mesas rise cleanly with realistic peak-to-width aspect ratios. Update `worldgen_golden.txt` cleanly once verified.

### Phase 3 — Erosion-Sculpted Mountains & Coupled Mountain-to-Sea Hydrology
- **Objective:** Replace extruded-looking ridges with dendritic drainage gullies, sharp arêtes, glacial cirques, talus/alluvial aprons, and rivers that actually drain *down* from high mountain ranges to sea level.
- **Approach:** Analytical/hierarchical stream-power incision & ridge-distance warping in `nd-world` (O(1) deterministic per sample, SIMD-friendly, bit-exact CPU/GPU), plus coupling `nd_field_eval_river_ex` to highland catchments so upland torrents carve gorges and feed lowland meandering rivers and deltas.

### Phase 4 — Climate-Coupled Altitude Zonation, Orographic Rain Shadows & Volumetric Cloud Ceiling
- **Objective:** Calibrate snowline, timberline, alpine tundra, and cumulus/stratus cloud ceilings to real physical meters (`uPlanetMetric`) and couple moisture to prevailing-wind topographic lift.
- **Approach:** Recalibrate `snowline_base_wu` (~`1,100–1,400 m` temperate at `b = 16`, latitude-shifted), crisp treeline (~`750–1,000 m`), and volumetric cloud LCL (~`900–1,500 m`) so tall peaks pierce cloud decks and cast/receive clean orographic cloud shadows without sign-flipping artifacts. Add upwind/downwind elevation sampling along `libcorolla` prevailing winds for lush windward slopes vs. ochre rain-shadow basins.

### Phase 5 — Photorealistic Micro-Relief, Strata-Ribbed Crags & Distant Canopy Massing
- **Objective:** Defeat the "toy model" trap at mid-to-far range (`200 m – 15 km`).
- **Approach:** Sharp slope- & curvature-driven rock buttresses with dipped sedimentary/metamorphic strata cutting cleanly through snowfields; high-frequency micro-normal detail scaled in physical meters; textured dark-green canopy massing shader with crisp treeline edges beyond 3D tree instancing range so distant foothills read as forests, not smooth green plastic.

### Phase 6 — Coastline Personality, Cliff/Bay Contrast & Glacial/Fluvial Landform Accents
- **Objective:** Make shores, estuaries, and valleys photographically distinct by geology.
- **Approach:** Couple wave-exposure & rock-hardness fields to coastal profiles (sheer sea cliffs & stacks on hard headlands; wide low-gradient sandy beaches & barrier spits in sheltered bays; deep drowned glacial fjords in cold high-relief provinces).

### Phase 7 — Camera Horizon Framing, Ridge-by-Ridge Aerial Perspective & Atmospheric Depth
- **Objective:** Deliver genuine photographic composition from ground level to stratosphere.
- **Approach:** Tune physical Rayleigh/Mie optical depth to `45 wu/m` so foreground hills, mid-ground ranges (`5–15 km`), and distant horizon peaks (`20–50 km`) separate into crisp blue atmospheric planes without washing out mountain silhouettes; verify coarse LOD mip preservation so distant sharp summits retain silhouette height and crispness.

### Phase 8 — Planet Scaling Activation (`planet_bits = 18..23`) & Server/World Coord Migration
- **Objective:** Enable opt-in larger worlds (`France`-scale `b = 18` up to `Earth`-scale `b = 22/23`) end-to-end.
- **Approach:** Widen `axil-nd` room coordinates and `ar_config_t` periods (`int64_t` for `b >= 23`), add world-creation `planet_bits` metadata, and validate stratosphere-to-ground rendering across `b = 16, 18, 20, 22, 23`.
