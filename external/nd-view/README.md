# axil-nd-view

`nd-view` for [axil-nd](../axil-nd), ported from SIC to libxylem.

Provides visual room perception, ANSI view buffer assembly, and entity looking mechanics.

## Role & Subsystem

`nd-view` aggregates room title, atmospheric descriptions, exits, present entities, dropped items, and terrain features into a cohesive visual representation. It transmits packed pure-C binary map grid structs (`nd_map_data_t`, opcode 1 `BCP_VIEW`) for client-side rendering (Bud ASCII, Lamina 2D Canvas, or Seven 2.5D/3D WebGL2), caches plant species lookups (`view_lookup_plant_spec`), and skips fallback ANSI string assembly (`BCP_VIEW_BUFFER`, opcode 2) when serving graphical WebSocket clients (`nd_player_is_ws`).

## In-Game Commands (`man-src/`)

| Command | Section | Description |
|---------|---------|-------------|
| `view` | 10 | Inspect room topography and visual overview |

## Packaged Assets

- **Art (`art/`)**: Viewport backgrounds and room aesthetic art.
- **Manuals (`man-src/`)**: View command documentation.

## Install & Build

```sh
make
make install
```
Installs `lib/libnd-view.so` and view assets.
