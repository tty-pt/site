# Ahead-of-Time (AOT) vs. On-Room-Entry Game Content Architecture

## 1. Executive Summary & Problem Statement

In continuous graphical 2D/3D client views (such as NeverDark's **Seven** stage renderer) layered over a spatial MUCK/MUD world, a fundamental design tension exists between:
1. **Geographically continuous world features**: Terrain, flora, boulders, stone deposits, ponds, and static landmarks that exist in the world regardless of player presence and should be visible from afar on the map and in the viewport.
2. **Dynamic room contents**: Mobile creatures, NPCs, players, dropped items, and ephemeral room inventory that only exist or activate when an actor enters a specific room.

### The Problem
Previously, **trees** (`nd-plant`) and **stones** (`nd-stone`) operated under two completely divergent mechanisms:
- **Trees** were generated deterministically ahead of time via coordinate climate noise (`on_noise`), serialized into map tiles with botanical glyphs (`'x'`, `'t'`, `'a'`, `'i'`), and rendered across the entire viewport canvas by the Seven stage renderer. Trees remained persistent across room transitions and never popped in or out.
- **Rocks and stones**, conversely, were only generated upon room instantiation (`on_spawn`) as raw room inventory objects (`OBJ`). They possessed no coordinate noise footprint and emitted no map glyphs. Consequently, the Seven renderer had no knowledge of their existence until the player crossed the room threshold and received BCP `ITEM` packets into `st->entities`. The renderer then blitted a stone sprite at one of the room's entity spots (`s_spots`). When the player stepped out of the room, `st->entities` cleared, and the stone instantly vanished.

### The Objective
This document details the architectural specification and implementation to:
1. **Unify rocks and stones with the Ahead-of-Time (AOT) pipeline**, ensuring they are procedurally known ahead of time, drawn with distinct glyphs/colors on the map, materialized into real interactive room inventory objects (`OBJ`) on spawn, and rendered continuously by the Seven renderer without popping.
2. **Establish a clean, generic, and extensible framework for ANY game contents**, allowing modders and engine developers to trivially add new ahead-of-time world contents or on-room-entry dynamic contents with zero architectural debt.

---

## 2. Deep Dive: How Trees Work (The 5-Stage Flora Pipeline)

To understand how rocks and stones should behave, we trace the complete lifecycle of trees across the NeverDark stack:

```
[Coordinate Noise] ---> [bio.raw Storage] ---> [Map Serialization] ---> [Seven Renderer] ---> [Room Spawn]
 (nd_world/noise)         (plant_tile_t)         (nd_view/view.c)        (seven-world.h)       (HD_OBJ)
```

### Stage 1: Coordinate Climate Noise Generation
- **Source**: `external/nd-world/src/noise.c:131`
- **Hook**: `r = nd_evt_noise(r, _he, w, _tm, _cl);`
- **Listener**: `external/nd-plant/src/libnd-plant.c:355` (`XY_IMPL(struct bio, on_noise, ...)`)
- **Execution**: The engine computes octaves of elevation (`_he`), water level (`w`), temperature (`_tm`), and cloud/rain (`_cl`). When `he > w` (land), `nd-plant` seeds a pseudo-random generator with `PLANTS_SEED` and calls `plants_noise()`. This evaluates temperature and rainfall ranges for 10 plant species (defined in `data/plants.dsv`), determining species IDs and growth sizes (`n`).

### Stage 2: In-Tile State Encoding
- **Data Structure**: `plant_tile_t` (defined in `<nd/plant-types.h>`):
  ```c
  typedef struct {
      unsigned id[3];
      unsigned char n, max;
  } plant_tile_t;
  ```
- **Storage**: Packed into the 64-byte `raw` storage of `struct bio` (`memcpy(r.raw, &pd, sizeof(pd))`). `plant_tile_t` consumes 16 bytes (words 0..3 of `raw[16]`).

### Stage 3: Map View Serialization
- **Source**: `external/nd-view/src/view.c:535`
- **Hook**: `view_empty_cell(vt, side, &glyph, &fg)` -> `ss = nd_evt_empty_tile(*t, side, ss);`
- **Listener**: `external/nd-plant/src/libnd-plant.c:374` (`XY_IMPL(sic_str_t, on_empty_tile, ...)`)
- **Execution**: For each of the three horizontal cells of a map tile (side 0 left, side 1 centre, side 2 right), `nd-plant` inspects `PLANT_N(pd.n, side)`. If present, it looks up the species skeleton and writes the ANSI color code and botanical glyph (`'x'`, `'X'`, `'t'`, `'T'`, `'a'`, `'A'`, `'i'`, `'I'`) into `ss.str`. The view serializes this into `nd_map_tile_t` (`side_l`, `symbol`, `side_r`), transmitted to clients via `fbcp_view`.

### Stage 4: Continuous View Rendering (Seven Stage)
- **Source**: `external/nd-seven/include/nd/seven-world.h:286` (`ndw_tile_objects`) & `mods/nd/ux/nd_seven.c:draw_world_obj`
- **Execution**: The client Seven renderer receives the 7x7 grid of `nd_map_tile_t`. In `ndw_tile_objects()`, it iterates across each tile's glyphs (`t->glyph[s]`), invokes `ndw_plant_kind(g)` (`NDW_K_DECID`, `NDW_K_TALL`, `NDW_K_FIR`, `NDW_K_CACTUS`), and deterministically computes foot coordinates using a coordinate seed `ndw_seed(pos)`. Because the map provides data for all visible tiles, trees are drawn in world coordinates and do not pop when the player enters or leaves their room.

### Stage 5: Room Materialization & Interactive Inventory
- **Source**: `external/nd-plant/src/libnd-plant.c:179` (`XY_IMPL(int, on_spawn, ...)`)
- **Execution**: When a room is instantiated in the engine database (`on_spawn`), `nd-plant` reads `plant_tile_t` from `bio.raw` and calls `object_add()` for each plant present on the tile. This creates physical `OBJ` rows in `HD_OBJ` equipped with the `chop` action and stick drops. The in-room objects match the map glyphs.

---

## 3. Root Cause Analysis: Why Rocks and Stones Popped

Prior to this work, `nd-stone` lacked integration with Stages 1, 2, and 3:

```
[No Noise Hook]       [No bio.raw State]       [No Map Glyphs]
      |                       |                       |
      v                       v                       v
[Map sees blank]  ==>  [Seven draws nothing] ==> [Player enters room]
                                                      |
                                                      v
                                           [BCP ITEM sends stone]
                                                      |
                                                      v
                                        [add_entities pops sprite at s_spots]
                                                      |
                                                      v
                                           [Player exits room]
                                                      |
                                                      v
                                        [st->entities clears: stone vanishes]
```

1. **Missing Coordinate Noise**: `libnd-stone.c` did not participate in `on_noise`. Stones were not computed deterministically across spatial coordinates.
2. **Missing Map Representation**: `libnd-stone.c` did not participate in `on_empty_tile`. Map tiles contained no mineral glyphs (`'*'`, `'o'`, `'O'`).
3. **Room Spawn Disconnect**: `stones_add()` was called only inside `on_spawn`, using the spawn entropy hash `v` and `bio_idx`. It added stone objects to `HD_OBJ`, but these objects were invisible outside the room.
4. **Transient Entity Rendering**: Because stones were only known via `st->entities` upon room entry, `mods/nd/ux/nd_seven.c:add_entities` placed a stone sprite at `s_spots` inside the current room. Moving to an adjacent room instantly unmounted the sprite, producing jarring pop-in and pop-out.

---

## 4. The Extensible Game Content Architecture

To make this clean, modular, and easy to extend for any content type, we establish the following architectural patterns:

### A. The Two Content Archetypes

| Property | Archetype A: Ahead-of-Time (AOT) Contents | Archetype B: On-Room-Entry Dynamic Contents |
| :--- | :--- | :--- |
| **Examples** | Trees, rocks, boulders, minerals, ponds, ruins | Goblins, wandering mobs, NPCs, dropped loot, chests |
| **Generation** | Deterministic function of `(pos[4], bio)` in noise | Dynamic / event-driven on room creation or AI tick |
| **Map Presence** | Dedicated glyphs + ANSI colors in `map_data.tiles` | Generic entity marker (`!` or `E`) or hidden |
| **View Rendering** | Rendered by Seven from map tiles across viewport | Rendered in current room at entity spots (`s_spots`) |
| **Room Inventory** | Real `OBJ`s created via `on_spawn` from noise data | Created dynamically or populated from room state |
| **Viewport Popping** | **Zero popping** (continuous world objects) | Natural appearance (entities entering/spawning) |
| **Client Handling** | Filtered from `add_entities` to prevent duplicate pawns | Handled by `add_entities` as pawns/entity sprites |

### B. Single-Owner Decorator Chaining (`core_noise_decorate` & `core_empty_tile_decorate`)
Under `libxylem` (XY), co-implementing hooks with return values (`on_noise`, `on_empty_tile`) suffers from **last-wins overwriting** (`MODS.md:939`, `:994`).

Following the proven `core_icon_decorate` precedent (`external/axil-nd/MODS.md:1006`):
- `nd-core` is established as the **single owner** and sole `XY_IMPL` of `on_noise` and `on_empty_tile`.
- `nd-core` exposes registration APIs in `<nd/core.h>`:
  ```c
  typedef struct bio (*core_noise_fn)(struct bio bio, uint32_t he, uint32_t w, uint32_t tm, uint32_t cl);
  typedef sic_str_t (*core_empty_tile_fn)(view_tile_t t, unsigned side, sic_str_t ss);

  XY_DECL(int, core_noise_decorate, core_noise_fn, fn);
  XY_DECL(int, core_empty_tile_decorate, core_empty_tile_fn, fn);
  ```
- Modules (`nd-plant`, `nd-stone`, and future modules) register their decorators during `xy_install()`.

### C. Raw Storage Partitioning
`struct bio` and `view_tile_t` provide `unsigned raw[16]` (64 bytes). This storage is partitioned cleanly by domain:
- `raw[0..3]` (16 bytes): `plant_tile_t` (owned by `nd-plant`).
- `raw[4..7]` (16 bytes): `stone_tile_t` (owned by `nd-stone`):
  ```c
  typedef struct {
      unsigned id[3];
      unsigned char n, max;
  } stone_tile_t;
  ```
- `raw[8..15]` (32 bytes): Reserved for future AOT contents (ores, structures, water features).

During `on_noise`, `nd-core` threads `bio` through each decorator:
1. `nd-plant` computes flora and writes `raw[0..3]`.
2. `nd-stone` computes stones and writes `raw[4..7]` (always zeroing unused slots to prevent stale cache leakage from `chunks_bio_raw`).

### D. Cell Allocation & Priority Rules
Each map tile consists of 3 horizontal cells (side 0 left, side 1 centre, side 2 right):
1. **Left cell (side 0)**: Stairs Down (`<`) > Flora (`nd-plant`) > Mineral/Rock (`nd-stone`)
2. **Centre cell (side 1)**: Player (`!`) > Entity Flag / Shop (`vtf_find`) > Flora (`nd-plant`) > Mineral/Rock (`nd-stone`)
3. **Right cell (side 2)**: Stairs Up (`>`) > Flora (`nd-plant`) > Mineral/Rock (`nd-stone`)

In `on_empty_tile`, `nd-core` passes the cell through decorators in priority order:
- If `nd-plant` claims the cell, it returns non-space text.
- If the cell is unoccupied (`ss.str` is empty or `' '`), `nd-stone` checks its `stone_tile_t` for that side and populates the mineral glyph and ANSI color.

### E. Table-Driven Client Content Mapping (Seven Renderer)
In `external/nd-seven/include/nd/seven-world.h`, hardcoded plant switches are replaced with a clean table-driven content mapper:
```c
typedef struct {
    char glyph;
    uint8_t fg;
    uint8_t kind;
    uint8_t sprite_base;
    uint8_t sprite_count;
    uint8_t is_big;
    uint8_t hw, hh;
    uint8_t blocks_movement;
} ndw_content_entry_t;
```
Glyphs mapped include:
- `'x'/'X'`: Deciduous trees (`NDS_TREE_OAK`, `NDS_TREE_APPLE`, `NDS_TREE_ROUND`)
- `'t'/'T'`: Tall trees (`NDS_TREE_BIG`, `NDS_TREE_APPLE2`)
- `'a'/'A'`: Fir trees (`draw_fir`)
- `'i'/'I'`: Cacti (`draw_cactus`)
- `'*'`: Small stones (mapped by `fg` to `NDS_STONE_GREY`, `NDS_STONE_BLUE`, `NDS_STONE_TINY`, `NDS_STONE_FLAT`)
- `'o'`: Medium rocks (mapped by `fg` to `NDS_ROCK_TAN`, `NDS_ROCK_GREY`, `NDS_ROCK_TAN_SM`)
- `'O'`: Big boulders (mapped by `fg` to `NDS_ROCK_TAN_BIG`, `NDS_ROCK_WHITE_BIG`)

### F. Room Spawn Materialization & Client Deduplication
1. **Engine Materialization (`on_spawn`)**:
   `nd-stone` extracts `stone_tile_t` from `bio.raw[4..7]`. It creates interactive `OBJ`s via `object_add()` for the exact stone species generated by noise, maintaining 100% parity between map glyphs, Seven sprites, and interactive items.
2. **Client Deduplication (`add_entities`)**:
   In `mods/nd/ux/nd_seven.c:add_entities()`, any entity whose skeleton corresponds to an AOT world object (e.g. `type == type_plant` or `type == type_mineral`) is skipped from room entity pawn blitting. The object is already rendered as part of the world tile, so no popping occurs.

---

## 5. Developer Guide: Adding New AOT Game Content in 3 Steps

With this framework in place, adding a new content type (e.g. `nd-mushroom` or `nd-crystal`) requires only three steps:

### Step 1: Server Decorators & Storage
1. Assign a 16-byte slice in `bio.raw` (e.g. `raw[8..11]`).
2. In the new module's `xy_install()`, call `xy_load("libnd-core")` and register:
   ```c
   core_noise_decorate(my_noise_fn);
   core_empty_tile_decorate(my_empty_tile_fn);
   ```
3. In `on_spawn`, read `bio.raw` to instantiate interactive `OBJ` items.

### Step 2: Client Content-Kind Mapping
In `external/nd-seven/include/nd/seven-world.h`:
Add an entry to `ndw_content_table`:
```c
{ 'm', 1, NDW_K_MUSHROOM, NDS_MUSHROOM_RED, 2, 0, 4, 4, 0 },
```

### Step 3: Atlas Sprite Mapping
In `mods/nd/ux/nd_seven.c:draw_world_obj`:
Handle the kind in the render switch or let the table's `sprite_base` automatically blit the sprite.

---

## 6. Verification Matrix

| Requirement | Test Coverage | Verification Method |
| :--- | :--- | :--- |
| **R1**: Commit & Push Current Work | Git verification | Confirmed commit `c94b79e` pushed to `origin/main` |
| **R2**: AOT Stone Noise Generation | `test_seven_world.c` | Verify stone density computed deterministically from coordinates |
| **R3**: Map Representation for Stones | `test_seven_world.c` & `test_seven_render.c` | Verify stone glyphs (`*`, `o`, `O`) appear on map tiles |
| **R4**: Seven Renderer Continuous Rendering | `test_seven_render.c` | Verify rock/stone sprites drawn across visible tiles from map |
| **R5**: In-Room Spawn & Deduplication | `test_seven_render.c` | Verify no pop-in at `s_spots` when stepping into stone room |
| **R6**: Extensible Architecture | Framework structure | Verified single-owner decorator chaining in `nd-core` |
| **R7**: Comprehensive Analysis | Documentation | `docs/AOT_GAME_CONTENTS_ANALYSIS.md` deliverable |
