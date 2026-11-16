# IMPROVE.md — Terrain, Geology, Hydrology & Biome Vegetation Specification

This document specifies the architectural plan for fixing NeverDark Seven's biome vegetation contrast (Jungles vs. Deserts), 2D crescentic desert dunes, clean stratified geology (removing dirty concavity/slope blobs), and hypsometric water bodies (Option D: disabling broken noise-zero-crossing river/lake trenches now, reserving true macro-hydrology for a dedicated future phase).

---

## 1. Object Artwork & Standalone Asset Boundary (Deferred / Handled Externally)

### Invariant (Specified in [`external/nd-plant/docs/SPEC.md`](file:///home/quirinpa/site/external/nd-plant/docs/SPEC.md))
- Object sprites (flora and stones) are standalone PNG assets on disk under:
  - `external/nd-plant/art/plant/<slug>/<small|big>/<1..2>.png` (mirrored to `htdocs/nd/art/plant/<slug>/<small|big>/<1..2>.png`)
  - Future stone/object paths following the same standalone convention
- They are loaded dynamically via `lam_image_request()` / `lam_image_read()` into dynamic shelf slots in `mods/nd/ux/nd_seven_atlas_draw.c`.
- **Policy:** Do **not** overwrite or procedurally regenerate `art/plant/.../*.png` by cropping or kitbashing the legacy `tiles.png` tilemap. Artwork under `art/plant/` will be authored/supplied externally according to the camera perspective ($55^\circ$ pitch), scale ($1\text{ px} = 1\text{ wu} = 4\text{ cm}$), and anchor rules documented in [`external/nd-plant/docs/SPEC.md`](file:///home/quirinpa/site/external/nd-plant/docs/SPEC.md).
- **Botanical & Biome Decoration Expansion (`IDs 16..24`, Wire Capacity up to `31`):**
  - `ND_PLANT_PACK_ID` already supports up to **31 species** in bits `0..4`. While Phase 1 tightens the existing 16 species so temperate trees never leak into tropics and savannah trees/herbs never leak into deserts, **Phase 2 (Artist Collaboration, [`SPEC.md §7.3–7.4`](file:///home/quirinpa/site/external/nd-plant/docs/SPEC.md#L180-L210))** adds dedicated Tropical Jungle species (`ceiba_pentandra` Kapok with buttress roots, `roystonea_regia` Palm, `bambusa_vulgaris` Monsoon Bamboo, `cyathea_cooperi` Tree Fern, `heliconia_rostrata` Bird-of-Paradise) and Arid/Desert species (`opuntia_ficus_indica` Prickly Pear/Barrel Cactus, `artemisia_tridentata` Desert Sagebrush/Tumbleweed) alongside biome-specific stone/ground props (red sandstone hoodoos & bleached deadwood in deserts vs. mossy boulders, fallen logs & fern carpets in jungles).

---

## 2. Biome Vegetation Contrast: Making a Jungle Look Like a Jungle and a Desert Look Like a Desert

### 2.1 Root Cause of Current Uniformity
1. **Tight Room Object Cap & Small Per-Slot Cluster Count:**
   - Every room (`160 × 128` world units) carries `3` map-tile slots (`side = 0, 1, 2` in `plant_tile_t` / `ndw_tile_t`), and `ndw_tile_objects` (`external/nd-seven/include/nd/seven-world.h`) only spawns `count_base = 2, count_rand = 1` (`2–3` objects per slot) with hard limits `VIRGIN_MEMO_MAX_OBJ = 9` (`mods/nd/ux/nd_seven_motion.c`) and `NDW_MAX_OBJ = 12` (`external/nd-seven/include/nd/seven-world.h`).
   - `ndw_tile_objects` enforces a mandatory empty center box (`[56..104] × [44..84]`) in every room and wide minimum spacing (`min_d2 = 360` for big trees), preventing interlocking forest canopies.
2. **Desert / Savannah Over-Spawning:**
   - `BIOME_DESERT (14)` and `BIOME_COLD_DESERT (6)` have `max_trees = 1, sparse_tree_mask = 0` and no room-level sparsity mask, allowing a cactus/tree cluster and up to 2 herb clusters (`linum`, `bellis`, `daucus`) in almost every desert room.
   - `acacia_senegal` (`rn_min = 35`), `salix_babylonica` (`rn_min = 240, tmp_max = 115`), `linum_usitatissimum` (`rn_min = 20`), and `bellis_perennis` (`rn_min = 40`) have overly broad moisture envelopes (`rn` on the `0..512` scale) in `external/nd-plant/data/plants.dsv` and `external/nd-plant/include/nd/plant.h`.
3. **Discrete Room-Level Grass Tuft Gating:**
   - `nd_seven_eval_meadow_area` (`mods/nd/ux/nd_seven.c`) evaluates `mt.biome` once per room on a uniform `26`-unit grid. Any room in `BIOME_SAVANNAH (15)`, `BIOME_SHRUBLAND (7)`, or `BIOME_MEDITERRANEAN (4)` has `g.kind == NDW_G_GRASS` (`is_arid == 0`) and `bio_mult >= 0.72`, carpeting dry/ecotone soil right next to desert dunes with uniform grass tufts.

### 2.2 Structural Changes Required (Aligned with [`external/nd-plant/docs/SPEC.md`](file:///home/quirinpa/site/external/nd-plant/docs/SPEC.md))

#### A. Expand Room Object Capacity, Room Sparsity & Multi-Story Forest Layering
1. **Increase Per-Room Object Capacity:**
   - Raise `NDW_MAX_OBJ` in `external/nd-seven/include/nd/seven-world.h` from `12` to `32`.
   - Raise `VIRGIN_MEMO_MAX_OBJ` in `mods/nd/ux/nd_seven_motion.c` from `9` to `32`.
2. **Add Room-Level Sparsity (`sparse_room_mask`) to `nd_biome_flora_profile_t` (`external/nd-plant/include/nd/plant.h`):**
   - Keep `plant_tile_t`'s 3-slot wire contract (`id[3]`, `n`, `max`) intact for binary compatibility with `r.raw[16]` in `libnd-plant.c` and `ndw_tile_t`.
   - Add `uint8_t sparse_room_mask;` to `nd_biome_flora_profile_t`:
     - When `sparse_room_mask != 0` and `(ty & ((uint32_t)prof->sparse_room_mask << 12u)) != 0`, `nd_plant_noise_eval_bio` returns `0` (`pd` all zero = completely barren room).
     - **`BIOME_DESERT (14)` & `BIOME_COLD_DESERT (6)`:** `sparse_room_mask = 0x7u` (**7 out of 8 rooms (`87.5%`) are 100% barren sand/rock**; only 1 in 8 rooms spawns an isolated xerophyte).
     - **`BIOME_TUNDRA (2)`, `BIOME_ALPINE (3)`, `BIOME_SHRUBLAND (7)`, `BIOME_GRASSLAND (10)`:** `sparse_room_mask = 0x1u` (50% open rooms), with `sparse_tree_mask = 0x3u` or `0x7u` for rare lone trees/krummholz.
     - **`BIOME_MEDITERRANEAN (4)` & `BIOME_SAVANNAH (15)`:** `sparse_room_mask = 0x0u`, `sparse_tree_mask = 0x1u` (umbrella-thorn acacia / olive groves in 50% of rooms, alternating with open savannah/garigue vistas).
3. **Biome-Scaled Multi-Story Cluster Expansion in `ndw_tile_objects` (`external/nd-seven/include/nd/seven-world.h`):**
   - Scale per-slot cluster counts and spacing dynamically by `t->biome`:
     - **Rainforests (`BIOME_TROPICAL_RAINFOREST = 17`, `BIOME_TEMPERATE_RAINFOREST = 13`):**
       - Each `big` tree slot spawns **`4 to 6` canopy trees** (`12–18` overlapping canopy trees across the 3 slots) plus **`2 to 3` companion `small` understory saplings/shrubs** (`6–9` understory plants beneath the canopy = **`18–27` plants per room**).
       - Reduce minimum spacing `min_d2` from `360` to `196` (`14 wu`) for canopy trees and `81` (`9 wu`) for understory plants, and allow placement across the full room (no artificial empty rectangle in the center of roadless wilderness jungle rooms) so crowns interlock into a true jungle canopy.
     - **Dense Forests & Wetlands (`BIOME_CONIFEROUS = 8`, `BIOME_BOREAL = 9`, `BIOME_TEMP_SEASONAL = 12`, `BIOME_TROP_SEASONAL = 16`, `BIOME_WETLAND = 5`):**
       - Each `big` tree slot spawns **`3 to 4` canopy trees** plus **`1 to 2` `small` understory plants** (`10–18` plants per room) with `min_d2 = 225`.
     - **Deserts (`BIOME_DESERT = 14`, `BIOME_COLD_DESERT = 6`):**
       - When the `1-in-8` room roll succeeds, cap `count = 1` (a single isolated cactus or desert scrub plant, never a multi-tree cluster).

#### B. Strict 16-Species Climate & Ecological Envelopes (`plants.dsv` & `plant.h`, Rain Scale `0..512`)
Update `external/nd-plant/data/plants.dsv` and `s_nd_plant_specs[16]` in `external/nd-plant/include/nd/plant.h`:
- **`0 pinus_sylvestris`:** `tmp: -36..44, rn: 140..515` (Boreal, Coniferous, Alpine, cool Mediter.; excluded from Wetland `5` & Temp. Broadleaf `12`).
- **`1 pseudotsuga_menziesii`:** `tmp: -8..50, rn: 240..515` (Coniferous, Temp. Rainforest; excluded from Wetland `5` & Temp. Broadleaf `12`).
- **`2 betula_pendula`:** `tmp: -28..58, rn: 180..480` (Boreal, Woodland, Temp. Seasonal).
- **`3 linum_usitatissimum`:** `tmp: -15..95, rn: 55..420` (Grassland, Meadow, Shrubland; `rn_min = 55` prevents spawning in hyper-arid desert).
- **`4 betula_pubescens`:** `tmp: -36..44, rn: 220..515` (Tundra edge, Boreal, Alpine Meadow).
- **`5 abies_alba`:** `tmp: -37..34, rn: 245..515` (Strictly cold Boreal & Montane Coniferous).
- **`6 arthrocereus_rondonianus`:** `tmp: 60..115, rn: 0..135` (Strictly Hot Desert `14` & xeric border).
- **`7 acacia_senegal`:** `tmp: 64..115, rn: 95..420` (`rn_min = 95` keeps Gum Acacia in Tropical Savannah `15` & Seasonal `16`, out of barren Hot Desert sand dunes).
- **`8 daucus_carota`:** `tmp: 10..95, rn: 95..380` (Temperate Grassland, Woodland, Savannah).
- **`9 solanum_lycopersicum`:** `tmp: 34..115, rn: 110..515` (Warm Temperate, Mediterranean, Tropical understory).
- **`10 quercus_robur`:** `tmp: 28..65, rn: 220..480` (Woodland, Temperate Seasonal, Temp. Rainforest; never in tropical `tmp >= 66`).
- **`11 salix_babylonica`:** `tmp: 24..115, rn: 280..515` (`rn_min = 280` keeps Weeping Willow strictly in Wetland `5`, Riparian, and Rainforests `13, 16, 17`, out of dry Savannah `15`).
- **`12 acer_pseudoplatanus`:** `tmp: 28..65, rn: 250..515` (Temperate Seasonal, Woodland, Temp. Rainforest).
- **`13 olea_europaea`:** `tmp: 30..105, rn: 115..390` (Mediterranean `4`, Savannah `15`, Dry Woodland).
- **`14 bellis_perennis`:** `tmp: -24..90, rn: 90..480` (Alpine Meadow, Grassland, Woodland; `rn_min = 90` keeps daisies out of deserts).
- **`15 typha_latifolia`:** `tmp: -10..115, rn: 290..515` (Wetland `5`, Marsh, Rainforest floor sedge).

#### C. Continuous Climate-Driven Ground-Cover Density (`mods/nd/ux/nd_seven.c`)
Replace the coarse per-room biome switch in `nd_seven_eval_meadow_area` with continuous per-sample climate and moisture gating (`rn` on the `0..512` scale):
1. **Arid Cutoff (`rn` & Desert/Steppe Blending):**
   - Sample continuous rainfall `rn` and biome at the tuft position.
   - For **Deserts (`BIOME_DESERT = 14`, `BIOME_COLD_DESERT = 6`, `BIOME_ICE = 1`, `BIOME_VOLCANIC = 18`)** or wherever `rn < 95` (unless immediately adjacent to shoreline moisture `water_prox > 0.40`): **density is `0.0` (zero grass tufts)**.
   - For **Savannah / Shrubland / Grassland / Mediterranean (`95 <= rn < 220` or `bio == 15, 7, 4, 10`)**:
     - Use a **clumped tussock noise gate** (`patch_n > 0.64`) so only `15–25%` of grid cells spawn dry golden/ochre tussocks in tight clumps, leaving `75–85%` open sun-baked soil/sand between clumps.
   - For **Temperate Woodlands & Seasonal Forests (`220 <= rn < 350`)**:
     - Natural meadow clustering (`bio_mult = 0.85..1.15`).
   - For **Tropical & Temperate Rainforests / Wetlands (`rn >= 350` or `bio == 17, 13, 5`)**:
     - Reduce the meadow sampling step from `26 wu` to `16 wu` (`2.6×` cell density) with `bio_mult = 1.45..1.85`, producing a lush, continuous understory carpet of ferns, broad-leaf floor plants, and sedges beneath the tree canopy.
2. **Dappled Canopy Ground Shading (`mods/nd/ux/nd_seven_gl.c`):**
   - In `s_fs_terrain`, when forest humidity/canopy weight (`wTropRain`, `wTempRain`, `wTempSeas`, `wBoreal`) is high, apply soft overhead canopy dapple (`0.84..0.96` cool forest-floor shade with rich humus/moss undertones) so the jungle floor reads as shaded understory rather than open sun-bleached pasture.

---

## 3. Realistic 2D Crescentic & Transverse Desert Dunes

### 3.1 Root Cause of "Diagonal Stripes"
1. **In `external/nd-world/include/nd/field.h` (`lines 1796–1805`):**
   - `int32_t wind_proj = (wx * 759 - wy * 687) >> 10;` projects `(wx, wy)` onto a pure 1D diagonal line with zero along-crest variation or domain warp, then feeds `wind_proj` into `nd_field_sin_q15(dune_turn)`. This creates infinite, ruler-straight diagonal sine waves at 42°.
2. **In `mods/nd/ux/nd_seven_gl.c` (`lines 1519–1523`):**
   - `float rippleCoord = dot(vWorldPos.xy, vec2(wind.y, -wind.x)) * 0.55 + (nStd - 0.5) * 1.2;` computes another 1D planar sawtooth wave across the terrain.

### 3.2 2D Crescentic / Barchanoid Dune Formulation

#### A. Elevation Field (`external/nd-world/include/nd/field.h`)
Replace the 1D `wind_proj` sine wave in `nd_field_elev_at_ex` with a **2D domain-warped asymmetric barchanoid ridge function**:
1. **Prevailing Wind Coordinates (`u` downwind, `v` crosswind):**
   - Let `u = (wx * 759 - wy * 687) >> 10` (downwind axis) and `v = (wx * 687 + wy * 759) >> 10` (crosswind crest axis).
2. **2D Domain Warp + Crescentic Horn Phase Shift:**
   - Sample two low-frequency 2D value-noise fields `w1 = value_noise_2d(wx, wy, 7, ...)` and `w2 = value_noise_2d(wx, wy, 6, ...)` to warp both `u` and `v`:
     - Crosswind sinuosity: `v_warp = v + (w1 >> 5)`
     - Downwind crescent curvature: Compute a crosswind cell phase `c_phase` from `v_warp` (and `w2`) so that each dune lobe curves forward (downwind) at its horns and lags at its slip-face center, breaking infinite lines into finite, interlocking crescentic ridges (`barchanoid` chains).
3. **Asymmetric Windward / Leeward Profile:**
   - Instead of a symmetric `sin(phase)`, shape the dune profile over phase $\phi \in [0, 1)$:
     - **Windward stoss slope ($\phi \in [0, 0.72)$):** Smooth, gentle convex-upward rise ($h(\phi) = \phi / 0.72$ smoothed with cubic hermite).
     - **Sharp dune crest ($\phi \approx 0.72$):** Crisp ridge peak whose height is modulated along the crest by 2D envelope noise (`env_q8`), so crests naturally pinch out into interdune flats (`sabkha` / reg hollows) and merge with neighboring dunes.
     - **Leeward slip-face ($\phi \in [0.72, 1.0)$):** Steeper descent into the interdune trough.

#### B. Terrain Shader Dune & Sand Micro-Shading (`mods/nd/ux/nd_seven_gl.c`)
1. Remove the 1D diagonal `rippleCoord` sawtooth wave (`lines 1519–1527`).
2. Shade desert sand using the **actual 3D surface normal `N` and slope asymmetry** of the 2D crescentic dunes:
   - **Sunlit windward stoss face:** Warm golden-ochre / bleached silica highlights.
   - **Crest rim:** Subtle wind-blown rim highlight along the ridge line where curvature transitions from stoss to slip-face.
   - **Leeward slip-face:** Smooth, warm terracotta/amber self-shadowing (never black/grey), with subtle wind-warped 2D ripples that curve parallel to the local dune slope gradient rather than a global 45° diagonal line.

---

## 4. Clean Stratified Geology (Removing Dirty Dark Blobs & Black Rings)

### 4.1 Root Cause of "Dirty Dark Blobs"
In `s_fs_terrain` (`mods/nd/ux/nd_seven_gl.c:1437-1665`), five separate darkening multipliers stack on top of each other wherever `concavity` (the Laplacian of the `48×48` height grid) or `slope` is non-zero:
1. `concavity = clamp(rawCurv * 28.0 + microCavity * 1.15 - min(0.0, h0) * 0.45, 0.0, 1.0)` spikes to `1.0` in every minor dip.
2. `soilFactor` (`lines 1498–1508`) mixes dark brown `loamCol` (`* 0.78`) into every concave spot or mild slope (`slope > 0.04`).
3. `wWetland` / `wBoreal` / `humusEnv` (`lines 1592–1628`) mixes near-black peat (`vec3(0.16, 0.22, 0.15)`) and dark humus into concave patches.
4. `rockFactor` and `talusFactor` (`lines 1450–1494`) trigger at very low slopes (`slope > 0.065`) and multiply `rockCol` by `frac1 * 0.48 + frac2 * 0.62` (down to `0.38x`), creating black spiderwebs on mountain slopes and dark charcoal rings around every depression or bank.
5. `cavityAO` (`line 1658`) multiplies the final lit color by down to `0.48x` in concave/crevice spots, and `shoreWetMoisture` (`line 1664`) darkens shorelines by another `22%`.

### 4.2 Geological & Shading Redesign (`mods/nd/ux/nd_seven_gl.c`)

1. **Remove Isotropic Dark Blob Multipliers:**
   - Remove `microCavity` and `rawCurv * 28.0` black darkening from `cavityAO`.
   - Clamp ambient topographic shading to a clean, gentle range (`0.84 .. 1.04`) driven by large-scale directional relief (`N · L` and sky hemisphere normal `N.z`), shifting shadows toward **cooler sky/foliage hues** (sage in meadows, warm sienna in deserts, slate-blue in alpine) instead of multiplying by black.
   - Remove the dark `darkPeat` / `loamCol` concavity blotches on flat and rolling ground; keep soil/grass transitions broad (`fBm` scales `0.02..0.06`), cohesive, and harmonized in luminance.
2. **Raise Rock & Talus Exposure Thresholds to Real Cliff Angles:**
   - Raise `rockFactor` onset from `smoothstep(0.11, 0.28, slope)` to **`smoothstep(0.24, 0.44, slope)`** (and high-altitude alpine ridges `elev > 75.0`). Gentle hills, depressions, and river/lake banks (`slope < 0.24`) remain covered in natural soil, sand, or turf — completely eliminating the dark charcoal rings around depressions and water bodies.
   - Restrict `talusFactor` to genuine cliff-base scree aprons (`slope > 0.18` directly below steep rock faces `concavity > 0.35 && elev > 35.0`), using clean mineral pebble tones rather than black `talusPocket` multiplication.
3. **True Sedimentary & Metamorphic Rock Strata on Cliffs & Mountains:**
   - Replace the isotropic `frac1` / `frac2` spiderweb crack darkening (`lines 1456–1468`) with **continuous 3D geological bedding planes (`strata`)**:
     - Define elevation-sliced, tectonically tilted bedding coordinate:
       `float strataCoord = vWorldPos.z * 0.28 + nMacro * 3.5 + dot(vWorldPos.xy, vec2(0.012, -0.008));`
     - Derive multi-scale sedimentary/metamorphic bands (`sin(strataCoord)`, `sin(strataCoord * 2.7 + ...)`):
       - **Desert / Red-Bed Canyons & Mesas (`wDesert`, `wSavannah`):** Warm Navajo sandstone strata — alternating terracotta, ochre, buff cream, and iron-oxide rose bands along elevation contours.
       - **Temperate / Mediter / Karst Cliffs (`wMediter`, `wTempSeas`):** Crisp limestone and dolomite ledges — warm cream-grey, pale ochre, and cool slate bedding planes.
       - **Alpine / Boreal Peaks (`wBoreal`, high `elev`):** Clean granite and gneiss — cool silver-grey, quartz-rose, and slate-blue facets lit sharply by directional sunlight (`N · L`) and stepped structural ledges.

---

## 5. Water Bodies & Hydrology (Option D: Clean Hypsometric Basins Now, Macro-Hydrology Later)

### 5.1 Root Cause of Weird Water Shapes & Unexpected Locations
1. **`nd_field_eval_river` (`external/nd-world/include/nd/field.h:1347-1612`):**
   - Computes `v_trunk = v12 + 0.375 * v10 + meander` and carves a channel wherever `|v_trunk| < r_chan`. Because summing multiple 2D noise octaves creates local extrema, its zero-set forms **closed loops/moats** (e.g., the circular island moat in `01`) and **isolated blobs**.
   - Worse, `plain_gate_q16` (`lines 1527–1542`) gates the carving off whenever `rug_q8 <= 32` and `h_above_sea > 7`, chopping channels off in mid-course and producing **dead-end sausage trenches in deserts (`09`)** and **disconnected rocky puddles in forests (`08`)**.
2. **`nd_field_eval_mountain_lake` (`external/nd-world/include/nd/field.h:1233-1334`):**
   - Stamps rotated elliptical pits on an `8×8` room grid wherever `3/8` of cells pass a hash, creating artificial steep-rimmed craters.

### 5.2 Phase 1 (Immediate — Option D): Pure Hypsometric Basins
1. **Disable Synthetic Trench & Pit Carving in `nd_field_elev_at_ex` (`external/nd-world/include/nd/field.h`):**
   - Disable `nd_field_eval_mountain_lake` and `nd_field_eval_river` in `nd_field_elev_at_ex` (`lines 1743–1752`) so `water_level` is strictly the continuous regional sea/basin level (`sea_lvl << 4`) and terrain elevation is never artificially trenched by local noise zero-crossings.
2. **Resulting Water Bodies:**
   - All oceans, bays, fjords, inland lakes, islands, and lowland floodplains/marshes emerge naturally where the multi-scale continental + mesoscale + detail hypsometric elevation field dips below `water_level`.
   - **Guaranteed properties:**
     - Water is always hydrostatically flat across connected basins.
     - Shorelines are smooth, natural elevation contours with gradual beaches, mudflats, or coastal cliffs depending on true terrain slope.
     - Zero circular moats, zero dead-end desert water sausages, and zero disconnected forest puddles with black rock rims.
3. **Update World Unit Tests (`tests/unit/test_seven_world.c`):**
   - Ensure any test assertions in `tests/unit/test_seven_world.c` reflect the clean hypsometric terrain model and updated plant ecological envelopes.

### 5.3 Phase 2 (Future Roadmap): Connected Macro-Cell Hydrological Graph
When upland rivers are re-introduced in a dedicated future task, they must obey three non-negotiable hydrological laws:
1. **Directed Downhill Graph on Macro-Cells:** Rivers are defined on a coarse macro-grid (e.g., `16×16` rooms per cell) where each cell routes flow strictly to its lowest neighbor (`steepest descent`) until reaching `elevation < sea_level` (or an endorheic basin minimum).
2. **Monotonic Water Surface & Drainage Accumulation:** River width and discharge scale monotonically with upstream drainage area (weighted by regional rainfall `rn`), and the water surface elevation decreases monotonically along the spline from source to sea.
3. **Climate Coupling:** In arid zones (`rn < 45`), low-discharge channels are dry wadis/canyons (`is_water = 0`); only perennial trunks fed by humid headwaters carry water through arid regions.

---

## 6. Verification & Quality Gates

1. **Targeted Builds & Unit Tests (Tier 0 / Tier 1):**
   - Rebuild `mods/nd` (`make -C mods/nd`) and browser WASM (`make site-wasm`).
   - Run `sh scripts/check-module-boundaries.sh` and `sh scripts/check-wasm-imports.sh`.
   - Run standalone world/plant/render unit tests (`make test-fast` / `tests/unit/test_seven_world` & `tests/unit/test_seven_render`).
2. **Visual Proof Across Biomes (`art/biome-proof/`):**
   - Verify via rendered screenshots at `1.0x` and `0.43x` zoom that:
     - **Tropical Rainforest / Jungle:** Dense multi-story canopy + understory + lush ground cover, dappled floor shading.
     - **Subtropical Desert Dunes:** 2D crescentic/sinuous barchanoid dunes with warm stoss/slip-face shading, zero grass tufts on open sand, 85–90% barren rooms with only rare xerophytes, and zero artificial water trenches.
     - **Savannah / Ecotones:** Smooth continuous transition from barren sand dunes to clumped golden tussocks and scattered acacia groves.
     - **Temperate Forest & Upland Mountains:** Clean, cohesive forest floors without dirty black concavity blobs, and crisp stratified rock bedding planes on steep mountain cliffs (`slope > 0.24`).
     - **Lowland Floodplain / Coastal Basin:** Natural hypsometric shorelines and islands without horseshoe moats or black rock collars.
