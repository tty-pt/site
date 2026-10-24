# axil-nd-view

`nd-view` for [axil-nd](../axil-nd), ported from SIC to libxylem.

Provides visual room perception, ANSI view buffer assembly, and entity looking mechanics.

## Role & Subsystem

`nd-view` aggregates room title, atmospheric descriptions, exits, present entities, dropped items, and terrain features into a cohesive visual representation. It transmits both ANSI strings (`BCP_VIEW_BUFFER`) and structured data (`BCP_VIEW`) to connected clients.

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
