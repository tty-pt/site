# Seven-Ways Collision Map Analysis & 2.5D Obstacle Modeling

## 1. Background & Investigation of `~/seven-ways`

In `~/seven-ways`, map collision is evaluated in `src/view.c:view_collides()`:
```c
uint32_t mx = x - view_min[0], my = y - view_min[1];
if (view_flags[my * map_width + mx] & 0x1)
    return 0; // blocked
return ISLET_MISS;
```
`view_flags` is constructed during `view_load()` by querying `map/col0.png` across ground and obstacle layers (`z < layer_n`).
`col0.png` is generated from `resources/tiles.tsx` by `src/tmxc.c`:
- Any tile possessing `<property name="col" type="bool" value="true"/>` paints white (`0xFFFFFFFF`, solid bit 0 = 1).
- All other tiles paint black (`0xFF000000`, walkable bit 0 = 0).

## 2. 2.5D Perspective: Base Collision vs Shadow vs Foreground Overlap

Analysis of `resources/tiles.png` and `resources/tiles.tsx` reveals the foundational 2.5D design of `seven-ways`:
1. **Pebbles (`*` / `NDW_K_STONE`)**:
   - Small flat pebbles (3–4 px radius, 2 px height) lie flat on the ground.
   - In `tiles.tsx`, pebble tiles (rows 16–19) have no collision (`col=false`).
   - The player walks freely over small pebbles without any collision interruption.

2. **Huge Rocks & Boulders (`o` / `NDW_K_ROCK`, `O` / `NDW_K_BOULDER`)**:
   - In `tiles.tsx`, large rocks like `NDS_ROCK_WHITE_BIG` (50x62 px) have collision **only at the base**:
     - Bottom row 3 (`y = 48..62 px`): tiles `(22, 3)` and `(23, 3)` have `col=true`.
     - Upper rows 0, 1, 2 (`y = 0..47 px`): have `col=false`!
   - **Why upper rows do not collide**:
     - In 2.5D perspective, the upper body of the rock rises vertically into the air.
     - When the player is walking behind (north of) the rock, their feet are on the ground behind the rock.
     - The rock renders in front of the player (depth-sorted by foot Y position in `nd_seven_render`), obscuring the player's legs while the player walks freely behind the rock.
   - **Why shadows do not collide**:
     - Large rocks cast directional shadows to the right and bottom.
     - Shadow pixels are flat on the turf and do not obstruct the player's movement.
   - **Physical Ground Contact Base & Solid Mass Protection**:
     - The obstacle footprint covers the solid mass of the stone while leaving the top crest dome available for 2.5D visual overlap when the player is standing directly behind it:
       - Medium rock (`'o'`, `NDW_K_ROCK`): base `hw = 10, hh = 14` (20 units wide, 14 units tall for 45px sprite; dynamic `hw = 7, hh = 7` for procedural `draw_rock`).
       - Big boulder (`'O'`, `NDW_K_BOULDER`): base `hw = 13, hh = 22` (26 units wide, 22 units tall for 56-62px sprites).
     - By blocking through `hh = 22` (44px) on big boulders, the player cannot penetrate or walk "under" the big rock graphic, while the top 12-18px dome still renders in front of the player's legs when standing north of the rock.

## 3. Seven World & Stage Integration

In `external/nd-seven/include/nd/seven-world.h`:
- `ndw_find_content`:
  - `'*'`: `hw = 0, hh = 0` (walkable pebble).
  - `'o'`: `hw = 10, hh = 14` (medium rock solid body).
  - `'O'`: `hw = 13, hh = 22` (big boulder solid body).
- `ndw_blocked`:
  - Player foot box (`NDW_FOOT_HW = 5, NDW_FOOT_H = 5`) tests against the obstacle base `[o->x - hw, o->y - hh, o->x + hw, o->y]`.
  - Walking North into base: blocks at `o->y`.
  - Walking South into base: blocks at `o->y - hh`, strictly preventing walking under the big rock.
  - Walking behind (North of `o->y - hh`): clear, player walks behind the rock with top crest rendered in front.
  - Walking beside (over shadow): clear, player walks over the shadow.
