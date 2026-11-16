# axil-nd-world

`nd-world` for [axil-nd](../axil-nd), ported from SIC to libxylem.

Governs world geometry, rooms, directional doors, walking traversal, and teleportation.

## Role & Subsystem

`nd-world` maintains the spatial topology of NeverDark: room links, compass directions, locked portals, and coordinate-based or room-ref movement.

## In-Game Commands (`man-src/`)

| Command | Section | Description |
|---------|---------|-------------|
| `room` | 10 | Create or reconfigure a room entity |
| `door` / `undoor` | 10 | Link or sever directional portals between rooms |
| `walk` | 10 | Traverse a connected exit in a given direction (`n`, `s`, `e`, `w`, etc.) |
| `teleport` | 10 | Instantaneous relocation to a target room reference |
| `position` / `set-position` | 10 | Inspect or adjust spatial coordinates within a zone |
| `bio` | 10 | Set or query biome classification for a room |
| `x` | 10 | Shortcut for examining room features |
| `unwall` | 10 | Remove impassable barrier between coordinates |

## Shared Headers (`include/nd/`)

- `<nd/world-types.h>` & `<nd/world.h>` — Room topology, directional exits, and coordinate traversal primitives.
- `<nd/field.h>` — Continuous 3D spherical planetary climate/elevation fields, 19-biome Whittaker-style classification (`nd_biome_classify`), and ecological index sampling.
- `<nd/terrain.h>` — Runtime-parameterized room geometry (`nd_room_config_t`, `ROOM_CELLS`, `ROOM_TILES`), terrain splatting weights, and 1:1 server-client decorative slot hashes (`nd_room_decor_slot`, `nd_room_decor_subcell`).

## Packaged Assets

- **Art (`art/`)**: World map tiles, environmental textures, and architectural glyphs.
- **Manuals (`man-src/`)**: Movement and world-building documentation.

## Install & Build

```sh
make
make install
```
Installs `lib/libnd-world.so`, `include/nd/{world-types,world,field,terrain}.h`, `art/`, and `man/`.
