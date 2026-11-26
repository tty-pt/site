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

- `<nd/world-types.h>` & `<nd/world.h>` — Room topology, directional exits, and coordinate traversal primitives (`HD_FD`-driven `st_update()` connection iteration in `src/spacetime.c`).
- `<nd/field.h>` — Continuous 3D toroidal/spherical planetary climate/elevation fields, 19-biome Whittaker-style classification (`nd_biome_classify`), 4-wide SIMD/batched evaluators (`nd_field_eval_coarse_unwrapped_step_q12_4_4x`, `nd_field_ruggedness_q8_4x`, `nd_field_elevation_fine_step_q12_4_4x`, `nd_field_row_sample_4x`, `nd_field_sample_4x`), stride-aware Nyquist band-limiting, analytical `nd_field_elevation_bounds()`, scanline row context (`nd_field_row_ctx_t`), and shared 4096-entry room memoization (`g_nd_field_room_memo` via `ND_FIELD_MEMO_EXTERN`).
- `<nd/field_glsl.h>` — Canonical GLSL terrain evaluation mirror (`ND_FIELD_GLSL_CANONICAL_FN`) matching CPU elevation/climate math for GPU shaders.
- `<nd/terrain.h>` — Runtime-parameterized room geometry (`nd_room_config_t`, `ROOM_CELLS`, `ROOM_TILES`), terrain splatting weights, and 1:1 server-client decorative slot hashes (`nd_room_decor_slot`, `nd_room_decor_subcell`).

## Packaged Assets

- **Art (`art/`)**: World map tiles, environmental textures, and architectural glyphs.
- **Manuals (`man-src/`)**: Movement and world-building documentation.

## Install & Build

```sh
make
make install
```
Installs `lib/libnd-world.so`, `include/nd/{world-types,world,field,field_glsl,terrain}.h`, `art/`, and `man/`.
