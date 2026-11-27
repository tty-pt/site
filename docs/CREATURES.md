# Procedural Creature Architecture (`CREATURES.md`)

Pure-C, deterministic procedural 3D creature pipeline split cleanly across:

1. **`external/nd-mob`** — Dependency-free genotype, watertight skeletal mesh builders, and
   pure-C hierarchical FK pose evaluators (`mob_dna.h`, `mob_mesh3d.h`, `mob_anim.h`).
2. **`external/nd-mob-gl`** — Standalone GPU-skinned instanced OpenGL ES 3.00 / WebGL2
   multi-creature renderer (`libnd-mob-gl.so` / statically hosted in `mods/nd` and
   `apps/nd-native`) exposed via the two-header `libxylem` XY contract
   (`include/nd/mob-gl-types.h` + `include/nd/mob-gl.h`).
3. **`mods/nd/seven/`** — Host scene renderer collecting the local player (`D_LAMB`), remote
   players (`ND_EF_PLAYER`), and NPC creatures (`ND_EF_CREATURE` / species name match) into
   an `nd_mob_actor_in_t` frame array submitted via `nd_seven_gl_submit_mob_actors()`, then
   invoking `nd_mob_gl_draw_shadow()` in Pass A and `nd_mob_gl_draw_color()` in Pass C1.

No static `.glb`/`.fbx` assets, no runtime asset downloads, and zero creature-specific
hacks inside Seven's scenery `mesh3d` VBO or shaders.

---

## 1. Non-Negotiable Invariants

1. **Canonical Metric Scale (`1.0 m = 45.0 wu`):**
   * All skeletons and meshes in `external/nd-mob/include/nd/mob_mesh3d.h` are authored in
     **meters** (`scale_wu_per_m = 45.0f` supplied per actor in `nd_mob_actor_in_t`).
   * Reference anchor: the canonical young lamb (`age = 0.15f`, `base_scale = 0.72f`)
     stands `~0.50 m` (`22.6 wu`) tall with hooves flush at `min_z = 0.0f`.
2. **Coordinate Frame & Grounding:**
   * Local axes: `+X = East (right)`, `+Y = South (nose / forward)`, `+Z = Up`.
   * Every generated bind-pose mesh is centered in XY around `(0, 0)` and planted at
     `min_z = 0.0f` (hooves/claws flush on terrain `world_z`; piscine/avian actors use
     water depth or airborne altitude).
   * Pass A directional shadows transform vertices in world-grid space (`wp - uShadowOrigin`);
     Pass C1 color transforms vertices in camera-origin-relative torus tangent space
     (`rel_pos + dx*uEast + dy*uSouth + dz*uUp`).
3. **GPU-Skinned Instancing (`external/nd-mob-gl`):**
   * **Static bind-pose mega-VBO:** `48`-byte vertices (`nd_mob_gl_vert_t`: `pos`, `nrm`,
     `col`, `bone0`, `bone1`, `mat_id`, `ao_u8`, `bone_w`) uploaded once per cached species
     (`GL_STATIC_DRAW`, up to `ND_MOB_GL_MAX_SPECIES_CACHE = 32` species).
   * **Per-instance VBO:** `80`-byte descriptors (`nd_mob_gl_inst_t`, `5 × vec4` at
     attributes `5..9`, divisor `1`) holding `rel_pos`, `world_pos`, `yaw`, `scale`,
     `bone_slot`, `water_z`, `alpha`, and `owner_glow`.
   * **Bone-pose texture (`GL_TEXTURE14`, `ND_MOB_GL_BONE_TEX_UNIT = 14`):** `72 × 256`
     `GL_RGBA32F` texture (`MOB_MAX_BONES * 3` columns × `ND_MOB_GL_MAX_ACTORS` rows),
     storing 3-row (`3 × vec4`) affine `3×4` bone matrices per actor evaluated on CPU via
     `mob_eval_pose()` and sampled via `texelFetch()` in both shadow and color vertex
     shaders for 1:1 shadow-map parity.
   * **Batching:** Actors are grouped by species key `(species, age_bucket, coat_state,
     pattern_type, color_primary)` and rendered with **one `glDrawElementsInstanced` call
     per active species**.
4. **Watertight Manifold Geometry:**
   * Bone segments are emitted as closed, outward-CCW domed capsules and sculpted limbs,
     fins, wings, beaks, eyes, ears, and hooves.
   * Every directed edge `(u -> v)` must have a matching twin `(v -> u)` (zero open holes),
     positive signed volume, unit normals, and baked per-vertex AO in `[0.4, 1.0]`.
     Enforced across all 3 archetypes and all 19 species by `tests/unit/test_mob_mesh3d.c`.
5. **Biological Sub-Materials & Early-Z Opaque Routing:**
   * `mob_mat_id_t` (`MOB_MAT_WOOL = 0`, `MOB_MAT_SKIN = 1`, `MOB_MAT_GLOSS = 2`) drives
     material response inside `external/nd-mob-gl/src/mob_gl_shaders.h`:
     * `MOB_MAT_WOOL` (`0`): wrapped diffuse, velvet sheen rim, warm subsurface scattering.
     * `MOB_MAT_SKIN` (`1`): smooth coat / scales / feathers with subtle sheen and warm ear/fin
       translucency.
     * `MOB_MAT_GLOSS` (`2`): sharp specular catchlights on eyes, beaks, hooves, and horns
       with attenuated owner-glow so facial detail stays crisp.
   * Pass C1 renders creatures opaque (`glDisable(GL_BLEND)`, `glDepthMask(GL_TRUE)`,
     stencil `0x01` / clear seabed `0x08`) before 2D billboards and water.

---

## 2. Canonical Data Layouts & Supported Species

### `external/nd-mob/include/nd/`

* **`mob_dna.h`** — Compact genotype, state, and deterministic name lookup:
  * `creature_archetype_t`: `CREATURE_ARCH_QUAD_UNGULATE`, `CREATURE_ARCH_PISCINE`,
    `CREATURE_ARCH_AVIAN` (plus reserved `QUAD_CARNIVORE`, `BIPED`).
  * `creature_species_t` (19 supported species):
    * **Ungulates (5):** `lamb`, `sheep`, `cow`, `horse`, `pig` (wool tufts gated by
      `coat_density >= 0.55f`, so horse/cow/pig render smooth-coated).
    * **Piscine (8, matching `external/nd-mob/data/mobs.dsv`):** `goldfish`, `salmon`,
      `tuna`, `koifish`, `dolphin`, `shark`, `moonfish`, `rainbowfish`.
    * **Avian (6, matching `external/nd-mob/data/mobs.dsv`):** `woodpecker`, `icebird`,
      `parrot`, `swallow`, `sparrow`, `owl`.
  * `creature_dna_from_name(name, seed, &dna, &state)` resolves case-insensitive entity
    names to deterministic species DNA and default state.
* **`mob_mesh3d.h`** — Unified watertight mesh dispatcher:
  * `mob_build_mesh(&dna, &state, &mesh)` dispatches across `mob_mesh_build_ungulate()`
    (19 bones), `mob_mesh_build_piscine()` (11 bones: nose, head, 4-segment spine, tail
    fluke, dorsal/pectoral fins, glossy eyes), and `mob_mesh_build_avian()` (15 bones:
    pelvis, chest, neck, head, beak, tail fan, 2-segment wings L/R, legs L/R, glossy eyes).
* **`mob_anim.h`** — Pure-C hierarchical FK pose evaluator:
  * `mob_eval_pose(archetype, &skel, &params, &pose)` evaluates:
    * **Ungulate:** idle breathing/graze, diagonal trot, bounding canter, cornering bank,
      hop + landing squash, swimming paddle, terrain slope grounding.
    * **Piscine:** traveling sinusoidal S-wave spine/caudal undulation, pectoral fin scull,
      pitch/roll banking.
    * **Avian:** symmetric flapping wingbeats (inner shoulder + outer primary wrist lag),
      gliding thermal soar, tail fan spread, neck bob.

### `external/nd-mob-gl/include/nd/`

* **`mob-gl-types.h`** (shared types, included by implementer `libnd-mob-gl.c`):
  `nd_mob_actor_in_t`, `nd_mob_shadow_pass_t`, `nd_mob_frame_t`, `nd_mob_gl_stats_t`,
  `nd_mob_gl_register_mem()`, and shader source getters (`nd_mob_gl_*_src()`).
* **`mob-gl.h`** (caller `XY_DECL` hooks, included by `mods/nd/seven/` and tests):
  `nd_mob_gl_init`, `nd_mob_gl_reset_context`, `nd_mob_gl_sync`, `nd_mob_gl_draw_shadow`,
  `nd_mob_gl_draw_color`, `nd_mob_gl_get_stats`.

---

## 3. Integration Boundaries

* **`external/nd-race` (`data/races.dsv`):** RPG character-creation stat modifiers only.
* **`external/nd-mob`:** Mob `SKEL` templates (`data/mobs.dsv`), biological DNA, watertight
  procedural mesh builders, and pure-C skeletal FK math.
* **`external/nd-mob-gl`:** Independent OpenGL ES 3.00 / WebGL2 GPU-skinned creature
  renderer with zero dependency on Seven, torus math, or `mods/nd/ux`. Compiles as a
  standalone `libxylem` module (`libnd-mob-gl.so`) and embeds cleanly into `mods/nd/nd.so`,
  `htdocs/nd.wasm`, and `apps/nd-native`.
* **`mods/nd/seven/`:** Scenery `mesh3d` (`ND_SEVEN_MESH3D_TOTAL = 214`, `GL_STATIC_DRAW`)
  is strictly vegetation/rocks/geobreaks. `nd_seven.c` collects `D_LAMB` and creature/player
  `D_ENTITY` records into `nd_mob_actor_in_t[]` and calls `nd_seven_gl_submit_mob_actors()`;
  `nd_seven_gl_billboard.c` computes torus tangent coordinates + ground slope and runs
  `nd_mob_gl_draw_shadow()` in Pass A and `nd_mob_gl_draw_color()` in Pass C1.

---

## 4. Verification & Next Phases

### Verified
* `tests/unit/test_mob_mesh3d.c`: Watertight manifold, positive volume, unit normals, AO
  bounds, 2-bone skinning weights, SO(3) pose orthonormality, and motion deltas across all
  3 archetypes and all 19 species.
* `tests/unit/run-test-nd-mob-gl.sh` (`tests/unit/test_nd_mob_gl.c` + `glslangValidator`):
  XY `xy_load_mem` hook dispatch, 19-species batching and bone-matrix sync, plus strict
  GLSL ES 3.00 compile+link verification of both `mob_shadow` and `mob_color` programs.
* `tests/unit/run-test-seven-render.sh`: Verifies `ND_SEVEN_MESH3D_TOTAL == 214`, zero
  legacy `isCreature*` code in Seven's scenery shaders, and live multi-species submission
  through `nd_seven_gl_submit_mob_actors()`.

### Next Concrete Phase (Planned)
* **Server-to-Client DNA & Entity Lifecycle Wiring:** Extend BCP entity snapshots and
  `nd-mob` `SKEL`/`OBJ` metadata with compact creature visual fields (`species_id`, `seed`,
  `age_u8`, `coat_state`, continuous velocity/yaw) so remote players and wild NPC mobs
  reflect server-authoritative traits and smooth networked locomotion.
