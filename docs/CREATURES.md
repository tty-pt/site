# Procedural Creature Architecture (`CREATURES.md`)

Pure-C, deterministic procedural 3D creature pipeline shared between `external/nd-mob`
and the `mods/nd/seven/` WebGL2 / native OpenGL renderer. Creatures are generated from
compact DNA structs directly into the Seven mega-VBO—no static `.glb`/`.fbx` assets and
no runtime asset downloads.

---

## 1. Non-Negotiable Invariants

1. **Canonical Metric Scale (`1.0 m = 45.0 wu`):**
   * Skeletons and meshes in `external/nd-mob/include/nd/mob_mesh3d.h` are authored in
     **meters**, then scaled by `45.0f` when baked into `mods/nd/seven/nd_seven_mesh3d.c`.
   * Reference anchor: the canonical lamb body height is ~`18.0 wu` (`0.40 m`). The
     default young lamb template (`age = 0.22f`, `base_scale = 0.72f`) builds to
     `0.503 m` (`22.6 wu`) tall, `0.711 m` (`32.0 wu`) long, `0.338 m` (`15.2 wu`) wide.
   * `pack_mesh3d_instance()` renders creature templates (`ND_MESH3D_LAMB_ID`) at `1:1`
     metric scale (`scale_xy = scale_z = 1.0f`) rather than rescaling by 2D billboard
     sprite pixels.
2. **Coordinate Frame & Grounding:**
   * Local axes: `+X = East (right)`, `+Y = South (nose / forward)`, `+Z = Up`.
   * Every generated mesh is centered in XY around `(0, 0)` and planted at exact
     `min_z = 0.0f` (hooves/feet sit flush on terrain `b->z`).
   * `nd_seven_gl_eval_mesh3d_anchor()` and `mesh3d_shadow_north_shift()` apply
     `north_shift = 0.0f` for `ND_MESH3D_LAMB_ID` (unlike trees/rocks, which shift north
     by `anchor_y * 0.5f`).
3. **Deterministic Facing (No Coordinate-Hash Jitter):**
   * `mods/nd/seven/nd_seven.c` packs `(nd_seven_get_dir() & 3u)` (`DIR_DOWN=0`,
     `DIR_UP=1`, `DIR_LEFT=2`, `DIR_RIGHT=3`) into `lam_billboard_gpu_t.pad0` bits `0..1`
     alongside bit `7` (`0x80u`, in-water flag).
   * `pack_mesh3d_instance()` maps `dir` to `yaw01 = { 0.50f, 0.001f, 0.75f, 0.25f }`.
     The vertex shader computes `yaw = (yaw01 - 0.5) * 2π`, mapping local `+Y` to
     `(-sin(yaw)*E + cos(yaw)*S)` → South, North, West, East respectively.
4. **Watertight Manifold Geometry:**
   * Bone segments are emitted as closed, outward-CCW domed capsules
     (`mob_emit_bone_capsule`: south pole + 4 rings + north pole).
   * Every directed edge `(u -> v)` must have a matching twin `(v -> u)` (zero open
     holes), positive signed volume, unit normals, and baked per-vertex AO in
     `[0.4, 1.0]`. Enforced by `tests/unit/test_mob_mesh3d.c`.
5. **Material Band, Early-Z Opaque Routing & Glow:**
   * `ND_MAT3D_CREATURE_WOOL = 0.62f` (`mods/nd/seven/nd_seven_mesh3d.h`) sits in
     `(0.55, 0.70)`, strictly disjoint from alpha-tested foliage `[0.195, 0.425]` and
     mineral/rock `[0.70, 0.85)`.
   * Because `nd_seven_mesh3d_is_opaque()` classifies creature meshes as opaque, they
     render in the Early-Z opaque mesh3d pass (zero alpha-test discard) with dedicated
     wool/fleece shading (`isCreature` in `mods/nd/seven/nd_seven_gl_shaders.c`).
   * Universe-owner avatars (`style == ND_BB_STYLE_GLOW`, `isLambGlow`) enter the same
     mesh3d pipeline (never dropped to 2D sprites), receiving warm self-luminance and
     soft Fresnel rim glow while excluding `uLambLight` self-illumination at `d² ≈ 0`.
6. **Distance Fallback & Mega-VBO Budget:**
   * `nd_seven_gl_billboard_is_mesh3d_only()` returns `false` for `ND_MESH3D_LAMB_ID`,
     so beyond the 3D mesh draw horizon (`max_m3d_dist`) creatures crossfade smoothly back
     to their 2D atlas impostor sprite.
   * Seven's shared template buffers hold `ND_MESH3D_MAX_VERTS = 393,216` vertices and
     `ND_MESH3D_MAX_INDICES = 1,572,864` indices (`mods/nd/seven/nd_seven_atlas_draw.c`).
     One young lamb template uses `544` vertices / `3,048` indices (~0.14% of VBO budget).
     `nd_m3d_build_lamb_spec()` uses transient `malloc`/`free` scratch buffers so zero
     permanent WASM `.bss` is wasted.

---

## 2. Canonical Data Layouts

All procedural creature types live in dependency-free C headers under
`external/nd-mob/include/nd/`:

* **`mob_dna.h`** — Compact genotype & runtime state:
  * `creature_archetype_t`: `QUAD_UNGULATE`, `QUAD_CARNIVORE`, `AVIAN`, `PISCINE`, `BIPED`.
  * `creature_species_t`: `LAMB`, `SHEEP`, `COW`, `HORSE`, `PIG` (initial ungulate set).
  * `creature_dna_t`: `archetype`, `species`, `pattern_type`, `base_scale` (adult scale in
    meters), 11 normalized morphology genes (`torso_length`..`joint_thickness`), 6 appendage
    genes (`horn_size`..`coat_density`), `color_primary`/`secondary`/`tertiary` (RGBA8),
    `pattern_scale`, and `seed`.
  * `creature_state_t`: `age` (`0.0` newborn → `1.0` elder), `weight`, `coat_state`
    (`0 = normal`, `1 = sheared/molted`), `seed`.
* **`mob_mesh3d.h`** — Skeleton & mesh builder:
  * `mob_ungulate_bone_t`: 19-bone topology (`MOB_MAX_BONES = 24`: pelvis, mid-spine,
    chest, neck, head, snout, ears L/R, horns L/R, tail, 4 upper legs, 4 lower legs).
  * `mob_bone_t`: `parent`, head/tail endpoints `(hx,hy,hz)` → `(tx,ty,tz)` in meters,
    radii `r0, r1`.
  * `mob_vtx3d_t`: position, normal, RGBA color, baked `ao`, `bone_idx`, `bone_weight`.
  * `mob_mesh3d_t`: caller-owned `vtx`/`idx` buffers, counts, capacities, and exact metric
    bounding box (`min_x..max_z`).

---

## 3. Integration Boundaries with Existing `axil-nd` Modules

Keep module responsibilities strictly separated:

* **`external/nd-race` (`data/races.dsv`):** Owns **RPG character-creation stat
  modifiers** (`human`, `elf`, `dwarf`, `halfling`, `half-orc` → `str/con/dex/int/wis/cha`,
  `weapon_word`). It is **not** a 3D mesh archetype table.
* **`external/nd-mob` (`src/libnd-mob.c`, `data/mobs.dsv`):** Owns mob `SKEL` templates
  (`max_art`, `element`, `biomes`, `y`, `lvl`, `flags`, `race`) and `on_spawn` logic
  (currently registering opaque `fish` and `bird` race IDs via `race_add()`). Mapping a
  mob species/race to a `creature_archetype_t` and species DNA centroid belongs here.
* **`mods/nd/seven/` (`nd_seven_mesh3d.c`, `nd_seven_atlas_draw.c`, `nd_seven_gl_billboard.c`,
  `nd_seven_gl_shaders.c`):** Owns client-side template baking into the mega-VBO
  (`ND_MESH3D_LAMB_ID = 214`, `ND_SEVEN_MESH3D_TOTAL = 215`), per-instance GPU packing,
  Early-Z opaque rendering, glow/waterline shaders, and 2D impostor fallback.
* **`external/axil-nd` (`include/nd/bcp.h`) & Corm persistence:** Server-side `OBJ`/`SKEL`
  records and BCP entity snapshots do **not yet** transmit `creature_dna_t` or
  `creature_state_t`. Today, only the local player avatar (`D_LAMB` in `nd_seven.c`) binds
  to `ND_MESH3D_LAMB_ID` with `creature_dna_lamb_default(42u)` at `age = 0.22f`.

---

## 4. Implementation Status & Roadmap

### Implemented (Phases 0–4: Sculpted Lamb, Multi-Material Shading & Procedural Skeletal Animation)

* **Phase 0 — Data Layout (`mob_dna.h`):** `creature_dna_t`, `creature_state_t`, and
  `creature_dna_lamb_default()` defined and shared between native and WASM builds.
* **Phase 1 — `QUAD_UNGULATE` Skeleton & Aging Curves (`mob_mesh3d.h`):**
  `mob_skel_build_ungulate()` computes the 19-bone metric skeleton from DNA + `age`
  (larger relative head/ears on young animals, lengthening limbs, emerging horns at
  maturity, subtle elder spine hunch).
* **Phase 2 — Watertight Capsule Mesh & Seven Integration:**
  `mob_mesh_build_ungulate()` emits closed outward-CCW domed capsules with woolly crown
  tuft, per-vertex AO, exact `z = 0` grounding, and metric bounds; baked into
  `ND_MESH3D_LAMB_ID = 214` and rendered in both `htdocs/nd.wasm` and `apps/nd-native`.
* **Phase 3 — Sculpted Silhouette, Sub-Materials & Stable Wool Shading (`mob_mesh3d.h`, `nd_seven_gl_shaders.c`):**
  * Multi-lobe fleece puffs (shoulder/haunch/belly/back/cheek wool tufts), cupped
    elliptical ears with warm inner face, 3D glossy dark eyes with crisp white specular
    catchlights, sculpted nose pad, and flared cloven hooves (`876` vertices / `4,920`
    indices, ~0.22% of mega-VBO budget).
  * Three dedicated creature sub-material IDs inside `(0.55, 0.70)`:
    `ND_MAT3D_CREATURE_SKIN = 0.58f`, `ND_MAT3D_CREATURE_WOOL = 0.62f`, and
    `ND_MAT3D_CREATURE_GLOSS = 0.66f` — eliminating world-space wool texture swimming,
    adding velvet wool rim + ear subsurface warmth + crisp eye specular, and attenuating
    owner `isLambGlow` on glossy eyes/hooves so facial detail stays sharp.
* **Phase 4 — 19-Bone Hierarchical FK Animation, 2-Bone LBS Skinning & Zelda/MMO Controls (`mob_anim.h`, `nd_seven_motion.c`, `nd_seven_gl_billboard.c`):**
  * Pure-C `mob_eval_ungulate_pose()` + `mob_skin_mesh()` evaluate idle breathing and
    graze, distance-locked diagonal trot & bounding canter, cornering head-lead/banking,
    expressive hop + landing squash, wading/swimming paddle, interact head-nudge, and
    terrain slope grounding; updated into the lamb's mega-VBO slice via `glBufferSubData`
    before Pass A for 1:1 shadow-map parity.
  * Camera-relative movement (`WASD`/Arrows/`HJKL` + LMB+RMB MMO run), speed tiers
    (stroll/trot/canter), smooth 360° lamb facing, Zelda default framing (`35°` pitch,
    `1.65x` zoom, over-the-shoulder look-at lift), `Space` hop, `F` interact, `C`
    recenter behind lamb, smooth held `Q/E` orbit, and damped wheel zoom.
  * Verified by `tests/unit/test_mob_mesh3d.c` (watertight manifold, 3-material tags,
    2-bone skinning weights, exact SO(3) orthonormality, gait/hop/graze pose deltas) and
    `tests/unit/test_seven_render.c` (slice dimensions, sub-material GLSL, camera-relative
    movement, diagonal normalization, hop arc, recenter behind lamb, and live pose updates).

### Next Concrete Phases (Planned — Not Yet Implemented)

* **Phase 5 — Procedural Coat Patterns & Additional Species Centroids:**
  1. Evaluate `pattern_type` (`SPOTTED`, `PATCHED`, `BLAZE_SOCKS`, `STRIPED`, `SPECKLED`)
     in deterministic 3D object space during bake.
  2. Add `QUAD_UNGULATE` centroids (`sheep`, `cow`, `horse`, `pig`) using the existing
     `mob_skel_build_ungulate` + `mob_mesh_build_ungulate` pipeline.
  3. Add `PISCINE` (spline + fin profiles for `mobs.dsv` fish) and `AVIAN` (for `mobs.dsv`
     birds), followed by `QUAD_CARNIVORE` and `BIPED`.
  4. Each new builder must pass watertight manifold and metric-bounds tests in
     `tests/unit/test_mob_mesh3d.c` before wiring into `mods/nd/seven/`.
* **Phase 6 — Server-to-Client DNA & Entity Lifecycle Wiring:**
  1. Extend BCP entity snapshots and `nd-mob` `SKEL`/`OBJ` metadata with compact creature
     visual fields (`species_id`, `seed`, `age_u8`, `coat_state`) so remote players and
     NPC mobs render with deterministic individuality.
  2. Any lazy/unobserved wild-mob materialization must build on the existing server
     persistence lifecycle documented in `docs/ND-PERSISTENCE-LIFECYCLE.md` (Corm DB
     ownership and eviction rules), not a separate ad-hoc storage path.
