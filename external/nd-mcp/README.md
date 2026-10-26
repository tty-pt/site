# axil-nd-mcp

`nd-mcp` for [axil-nd](../axil-nd), ported from SIC to libxylem.

Implements the **Binary Client Protocol (BCP `#b`)** for real-time WebSocket communication between the NeverDark engine and pure-C isomorphic clients (`nd.wasm`).

## Role & Subsystem

`nd-mcp` bridges the game engine's internal events (`HD_OBJ`, `HD_OBS`, player changes) into compact binary frames transmitted over WebSocket connections (`DF_WEBSOCKET`).

## Wire Protocol (`#b`)

Packets begin with the magic prefix `#b` followed by a 1-byte channel identifier (`iden`):

| Opcode | Name | Payload | Description |
|--------|------|---------|-------------|
| 0 | `BCP_ITEM` | ref, loc, flags, type, actions, name, unparse, icon, art | Full item/room/entity serialization |
| 1 | `BCP_VIEW` | `nd_map_data_t` (400B) | Packed binary 7x7 map grid (biomes, exits, doors, glyphs) |
| 2 | `BCP_VIEW_BUFFER` | string | ANSI/text formatted room buffer (fallback) |
| 3 | `BCP_ROOM` | exits bitmask | Room topology |
| 4 | `BCP_ENTITY` | flags | Entity status and attributes |
| 5 | `BCP_AUTH_FAILURE`| reason code (`int`) | Login/auth failure |
| 6 | `BCP_AUTH_SUCCESS`| `player_ref` (`uint32`) | Successful login |
| 7 | `BCP_OUT` | obj_ref, loc_ref | Entity left room / despawned |
| 8 | `BCP_TOD` | `tod` (`uint32`) | Time of day cycle |
| 9 | `BCP_ACTION` | id, label, icon | Dynamic client action button registration |
| 10 | `BCP_REGION` | stage_drv, map_drv, layout, flags | Hierarchical presentation governance frame |

Dynamic feature modules register channels on `HD_BCP` (e.g. `bcp_hp=10`, `bcp_stats=11`, `bcp_equipment=12`, `bcp_mp=13`).

## XY Hooks & Exports

- `fbcp(player_ref, len, iden, data)`: Generic BCP frame broadcast.
- `fbcp_view(player_ref, map)`: Emit 400-byte packed binary map grid (`nd_map_data_t`) over opcode 1.
- `fbcp_view_buf(player_ref, view_buf)`: Emit formatted ASCII room buffer over opcode 2.
- `fbcp_item(player_ref, obj_ref, dynflags)`: Serialize object to player.
- `mcp_bar(iden, player_ref, val, max)`: Send status gauge update.
- `mcp_action(player_ref, id, label, icon)`: Register client action button.
- `mcp_content_in(loc_ref, thing_ref)` / `mcp_content_out`: Room observation dispatch.

## Install & Build

```sh
make
make install
```
Installs `lib/libnd-mcp.so` and `include/uapi/mcp.h`.
