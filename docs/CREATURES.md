# Procedural Creature & Character Architecture (`CREATURES.md`)

A deterministic, DNA-driven procedural character pipeline for generating **all creatures (birds, fish, livestock, wildlife) and humans of all ages** with true individuality—without requiring a large 3D art team.

---

## Core Philosophy

1. **Body Plans (Archetypes):** A small set of topological skeletons (`QUAD_UNGULATE`, `QUAD_CARNIVORE`, `BIPED`, `AVIAN`, `PISCINE`, `SERPENTINE`, `ARTHROPOD`).
2. **Continuous Morphospace ("DNA"):** Every species is a centroid + variance in its archetype's parameter space. A lamb, a cow, a horse, and a pig share `QUAD_UNGULATE`—they are simply different coordinates in the same morphospace. Individual animals sample around their species centroid.
3. **Skeleton-First Skinning:** The mesh is generated directly from the bone graph, so skinning weights are computed automatically during mesh generation (zero manual weight painting).
4. **Continuous Aging:** `age` (`0.0 → 1.0`) is a runtime curve applied to morphology, appendages, and surface shaders—not separate models.

---

## Roadmap

### Phase 0 — Core DNA & Data Layout
* Define `creature_dna_t`:
  * `archetype_id` (skeleton topology)
  * `species_id` (centroid / parameter bounds)
  * `morph_genes[]` (normalized `0.0..1.0` floats: torso length, chest depth, belly roundness, shoulder/hip width, neck length/arch, head scale, snout length, limb length, joint thickness, etc.)
  * `appendage_genes` (horn curl/size, ear droop/size, tail length, mane/wool/plumage density)
  * `surface_genes` (primary/secondary/tertiary colors, pattern type, pattern scale, `seed`)
* Define `creature_state_t`:
  * `age` (`0.0 → 1.0`)
  * `weight` / `health` (affects belly/muscle radius)
  * `coat_state` (e.g., sheared/molted)
  * `seed` (deterministic RNG for subtle asymmetry and variation)

### Phase 1 — Skeleton-First Graph & Aging Curves (Proving Ground: Lamb)
* Build the `QUAD_UNGULATE` bone-graph generator from `creature_dna_t` + `age`.
* Implement **aging curves**:
  * Head-to-body ratio starts high at birth and drops toward maturity.
  * Limbs lengthen; horns, antlers, tusks, and manes grow from `0` at birth to full size at maturity.
  * Subtle spine hunch and gait changes appear at `age > 0.75`.
* **Verification target:** Lamb → Adult Ram/Ewe → Calf → Cow → Foal → Horse → Pig all produce plausible, distinct skeletons from the same generator.

### Phase 2 — Procedural Skinning & Automatic Bone Weights
* Generate watertight meshes directly from the bone graph:
  * **Swept Cross-Section Lofting** for clean low-poly topology, or
  * **SDFs + Dual Contouring / Marching Cubes** for organic shoulder, hip, and neck junctions.
* Compute bone skinning weights during mesh generation based on bone proximity/capsules.

### Phase 3 — Procedural Surface, Markings & Coat
* Drive coat, skin, feather, and scale patterns via **3D object-space procedural noise** in the shader (or baked per-instance) using `surface_genes` + `seed`.
  * Every cow's spots, every horse's blaze/socks, and every lamb's fleece are unique with zero UV seams.
* Add age-driven surface shifts (younger = softer/spotted/downy; elder = graying muzzle, desaturation, roughness variation).

### Phase 4 — Normalized Animation & Foot IK
* Author **one** normalized animation set per archetype (idle, walk, trot, gallop, eat, sleep).
* Apply **stride-length scaling** and **2-bone foot IK** at runtime so a tiny lamb, a heavy cow, and a tall horse all plant their feet cleanly on uneven terrain without foot-sliding.

### Phase 5 — Expanding Archetypes
* **`AVIAN`** (eagle, crow, sparrow): wing-fold/flap rig, beak hook/length, tail fan, plumage.
* **`PISCINE`** (fish): spline + fin profiles + vertex-shader swim wave (no full skeleton needed).
* **`BIPED`** (humans of all ages): shared humanoid rig, facial/body morphs, procedural/modular hair, beards, clothing layers, and age-driven posture/wrinkles.
* **`QUAD_CARNIVORE`** (wolf, fox, bear, cat): digitigrade/plantigrade hind-leg toggle, snout/jaw/claw parameters.

### Phase 6 — MMO Scale: LODs, Impostors & Instancing
* **Distance-based LODs** (simplified mesh / reduced bone count at medium range).
* **GPU-instanced impostors/billboards** for distant flocks, schools, and herds.

---

## Integration with Existing `axil-nd` Architecture

* **`external/nd-mob` (`libnd-mob.c`, `data/mobs.dsv`):** Already owns mob skeletons, `on_spawn` tile placement, and species definitions (currently `fish` and `bird` races). `creature_dna_t` / `creature_state_t` should extend or attach to mob/entity records (`OBJ` / `SKEL`) so each spawned mob gets a deterministic `seed` and `age` alongside its `race` and `species_id`.
* **`external/nd-race`:** Maps `race` (e.g. `fish`, `bird`, `ungulate`, `biped`) to the procedural **Archetype** topology, while `mobs.dsv` species entries (`goldfish`, `sparrow`, `lamb`, `cow`, `horse`, etc.) map to **Species Centroids** in that archetype's morphospace.
* **`mods/nd/seven/nd_seven_gl_billboard.c` & `nd-plant` (`plant_mesh3d.h`):** Procedural 3D trees are already generated and instanced in `nd_seven_gl_billboard.c`. Creature mesh generation and GPU instancing should follow the same pure-C / WebGL2 pattern—generating vertex/index buffers and bone weights on the client from the entity's deterministic DNA + `age` without shipping heavy static `.glb`/`.fbx` assets.

---

## Deterministic Placement, Movement & Zero-Cost Unobserved Lifecycle

To support **up to 64K planets** without spending server RAM, CPU, or Corm DB storage on empty space:

1. **Zero-Cost When Unobserved:** Unobserved creatures do **not** run AI ticks or occupy Corm DB rows.
2. **Deterministic Placement & Movement:** Creature presence, species, DNA `seed`, `age`, and semi-realistic wander/patrol position are computed deterministically from `(planet_id, tile_coords, world_time)` when a tile enters an observer's active range.
3. **Lazy Materialization on Interaction:** A creature only materializes as a stateful server entity (`OBJ` in Corm DB) when a player interacts with it—directly (combat, taming, shearing, herding, killing) or **environmentally** (e.g., locking/enclosing it inside a player-built house or pen).
4. **Eviction & Persistence:**
   * Unmodified, uncontained wild creatures are evicted cleanly when all observers leave—leaving zero DB footprint.
   * Interacted, tamed, **enclosed/housed**, or player-owned creatures persist in Corm DB (like player-built structures) subject to lifecycle/TTL rules, so a creature locked in a house stays there when the player returns.
