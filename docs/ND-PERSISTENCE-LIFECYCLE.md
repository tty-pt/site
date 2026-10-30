# NeverDark World & UX Architecture: Stone Picking Persistence and Consistency

## Overview

This document specifies how resource harvesting (specifically stones, rocks, boulders, and ground entities) is persisted in the database and maintained consistently across the **Seven 2D Stage Renderer** (`mods/nd/ux/nd_seven.c`), the **Lamina 2D Minimap Renderer** (`mods/nd/ux/nd_lamina.c`), and the server-side **Corm DB** spacetime engine.

---

## 1. Client-Side Optimistic Suppression

When a player approaches a stone or ground entity in the Seven renderer and triggers the harvest/get action:

1. **Detection & Command Generation (`mods/nd/ux/nd_seven_objects.c:seven_action_impl`)**:
   - `seven_action_impl()` checks distance between lamb coordinates `(lx, ly)` and tracked objects in `s_tracker.objs[k]`.
   - For minerals (`NDW_K_STONE`, `NDW_K_ROCK`, `NDW_K_BOULDER`), it matches the bound Corm DB entity ref (`s_tracker.objs[k].entity_ref`).
   - If not already picked (`!nd_seven_is_picked(entity_ref)`), it formats the command: `get #<entity_ref>\n`.

2. **Immediate Client Suppression (Zero-Latency Local Feedback)**:
   - On execution, `nd_seven_mark_picked(entity_ref)` inserts the entity's 32-bit ID into a bounded 128-entry circular ring buffer (`s_picked_refs`).
   - `s_tracker.objs[k].harvested = 1` marks the current room's tracked object harvested.
   - **Seven Stage (`nd_seven.c`)**: `is_tracked_obj_present()` returns `0` and `add_entities()` skips the entity, causing the stone to vanish immediately on the next animation frame (<16ms).
   - **Minimap (`nd_lamina.c:draw_tile_objects`)**: When rendering the player's current tile (`col == map->player_x && row == map->player_y`), `draw_tile_objects()` checks `s_tracker` and `nd_seven_is_picked()`, skipping mineral rendering immediately.

3. **Network Dispatch**:
   - The command `get #<ref>\n` is sent over WebSocket Channel 0 (interactive stream) to the system axil daemon.

---

## 2. Server-Side Persistence in Corm DB

1. **Command Processing (`external/nd-obj/src/object.c:do_get`)**:
   - The MUCK command processor dispatches `do_get()`.
   - `ematch_near()` resolves the entity ref from `#<ref>` in the player's room.
   - `do_get()` invokes `eng_object_move(thing_ref, player_ref)`.

2. **Corm DB Mutation (`external/nd-obj/src/object.c:eng_object_move`)**:
   - `what.location` is updated to `player_ref` (player's inventory) and committed to Corm DB (`HD_OBJ`).
   - `del_dup_value(contents_hd, last_loc, what_ref)` removes the entity from the room's `HD_CONTENTS` index.
   - `mcp_content_out(last_loc, what_ref)` broadcasts an `ND_BCP_OUT` packet (opcode 5) to room observers.
   - An updated `ND_BCP_ITEM` (opcode 0) packet is dispatched to the player reflecting `location == me`.

---

## 3. Server-Side Map Decorator Consistency

1. **View Construction (`external/nd-view/src/view.c:eng_view`)**:
   - When a room view or movement tick occurs, `eng_view()` evaluates the 7x7 grid (49 tiles) centered on the player.
   - For every tile `i`, `view_build_tile()` links `t->room = loc_ref`.
   - `view_empty_cell()` invokes `nd_evt_empty_tile()` to query module tile decorators.

2. **Stone Decorator Query (`external/nd-stone/src/libnd-stone.c:stone_empty_tile_decorate`)**:
   - Iterates `HD_CONTENTS` of `t.room`.
   - Searches for entities where `obj.type == type_mineral`.
   - **Since the stone was moved to inventory, `has_mineral` is 0.**
   - Registers room harvest in `s_harvest` with `harvested_at = now` under `STONE_RESPAWN_COOLDOWN` (180 seconds).
   - Returns empty string (`ss`), omitting mineral glyphs (`*`, `o`, `O`) from the tile cell.

3. **BCP View Broadcast (`ND_BCP_VIEW`)**:
   - `eng_view()` packages the 49 tiles into `nd_map_data_t` and sends it via BCP opcode 1 (`ND_BCP_VIEW`).
   - The tile for the room now has NO mineral glyph.

---

## 4. Room Exit & Neighbor Tile Consistency

When the player walks out of the room into an adjacent tile (e.g. North):

1. **Origin Shift & Neighbor Tile Rendering in Seven**:
   - Server moves player to the North room and transmits updated `nd_map_data_t` centered on the North room.
   - In this new 7x7 grid, the previous room is at `row = player_y + 1` (South neighbor tile).
   - In `nd_seven.c:nd_seven_render()`, neighbor tiles (`ti, tj != 0`) are queried via `nd_seven_tile_at()`.
   - Because the previous room's tile in `st->map_data.tiles` has no mineral glyph (`has_mineral == 0` in Corm DB), `ndw_tile_objects()` produces **zero** mineral objects for that tile.
   - The previous room viewed through the door or seam on the Seven stage shows NO stone.

2. **Minimap Rendering in Lamina**:
   - `nd_lamina.c:draw_tile_objects()` iterates tiles `(col, row)` in `map_data`.
   - For the previous room, `nd_seven_tile_objects()` produces zero mineral objects.
   - The minimap cleanly omits the stone in the previous room.

3. **Re-entering the Harvested Room**:
   - If the player returns to the room before `STONE_RESPAWN_COOLDOWN` expires, the server's map tile remains devoid of mineral glyphs.
   - In `st->entities`, the stone has `location == me` (inventory) and is not in `st->room.ref`.
   - The stone remains persistently absent until its cooldown respawn.
