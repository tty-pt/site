# NeverDark Procedural 3D Terrain & Cavern Architecture Roadmap

## 1. Executive Summary & Vision

NeverDark is built on a pure-C, high-performance foundation: an event-driven MUCK engine (`axil-nd`), an islet-based 4D Morton spatial database (`libislet`), an isomorphic Virtual DOM HUD (`libbud`), and pluggable rendering viewports (`liblamina`, ASCII terminal, and the continuous 2D stage "Seven").

While Seven introduced detailed, organic terrain by applying multi-octave value noise and signed distance fields, that geometry currently lives entirely on the client side in `external/nd-seven/include/nd/seven-world.h` and `mods/nd/ux/nd_seven_ground.c`. The server engine compiles the world into flat 2D tiles, completely discards terrain elevation, and restricts procedural generation strictly to the $z=0$ plane.

This document establishes the architectural roadmap to **evolve NeverDark's terrain system into a centralized, true 3D procedural engine**. The core will preserve its blazing-fast $O(1)$ microsecond response times for traditional ASCII/MUD clients, while simultaneously delivering continuous terrain elevation, slopes, cliffs, underground cavern systems, and sub-tile organic geometry to visual renderers (Seven 2D and future 3D engines).

---

## 2. Codebase Reality & Architectural Deficiencies Today

A rigorous audit of the repository reveals four critical gaps between server physics and client presentation:

### A. The 2D Chunk Matrix Bottleneck & Height Discarding (`external/nd-world/src/noise.c`)
- **Matrix stepping instead of point queries**: `noise.c` relies on dense $32 \times 32$ chunk matrix generation (`CHUNK_M = 1024`, `n_he`, `n_cl`, `n_tm`, `chunks_bio_raw`). Querying a single coordinate via `eng_noise_point()` forces generating up to four 1024-element matrices and running dense array copying (`spread()`).
- **Surface height is thrown away**: In `noise_full()` (`noise.c:130`):
  ```c
  register uint32_t _he = n_he[j], w = water_level(obits);
  r.tmp = temp(obits, _he, _tm, s[Y_COORD] + (j >> CHUNK_Y));
  r.rn = rain(obits, w, _he, _cl, r.tmp);
  r.bio_idx = _he < w ? 0 : bio_idx(r.rn, r.tmp);
  r = nd_evt_noise(r, _he, w, _tm, _cl);
  ```
  Height `_he` is only evaluated to check if the point is submerged (`_he < w`) and to pick a biome. **`_he` is never stored in `struct bio` (`nd/world-types.h:233`) or `view_tile_t`.** The world is irreversibly flattened into a 1-byte biome index.

### B. The Z-Axis Lock (`z = 0`)
- **Procedural noise ignores $z \ne 0$**: In `noise.c:248`:
  ```c
  void noise_view(struct bio to[VIEW_M], pos_t pos) {
      if (pos[2] == 0) {
          noise_chunks(pos);
          view_print(to, pos);
      } else
          memcpy(to, &empty, sizeof(empty)); /* Returns BIOME_VOLCANIC */
  }
  ```
- **Vertical navigation is blocked on natural terrain**: In `spacetime.c:253`:
  ```c
  int e_ground(unsigned room, enum exit e) {
      pos_t pos;
      if (e & (E_UP | E_DOWN))
          return 0;
      eng_map_where(pos, room);
      return pos[2] == 0;
  }
  ```
  Vertical movement (`E_UP`, `E_DOWN`) is blocked on procedural ground, preventing players from traversing slopes, climbing hills, or descending into natural caves.

### C. Client-Side Parallel Procedural Geometry in Seven
- Because `nd_map_data_t` transmits only a 7x7 grid of flat square tiles, Seven built its own procedural noise system:
  - Fixed-point value noise (`ndw_vnoise` in `seven-world.h:552`).
  - Multi-octave fBm cascade (`ndw_warp_axis` with octave shifts 10, 9, 8, 7, 5).
  - Rounded box distance fields (`NDW_CORNER_RX`, `NDW_CORNER_RY`) to bend square borders into bays, capes, and peninsulas.
- **The flaw**: The organic coastline and terrain features rendered by Seven do not exist on the server. The server believes the room is a rigid square. A future 3D renderer would have to write yet another noise system, causing complete visual and physical disagreement across clients.

### D. The BCP View Packet (`external/axil-nd/include/nd/bcp-types.h`)
- `nd_map_tile_t` (12 bytes) carries biome index, glyphs, ANSI colors, exits, and doors, but has no elevation field.
- The 7x7 grid in `nd_map_data_t` (604 bytes) represents a single horizontal slice at `pos[2]`.

---

## 3. The Unified 3D Procedural Architecture

### The Hybrid Heightfield + 3D Cavity Model ($H + C$)

Allocating full 3D voxel arrays ($32 \times 32 \times 32 = 32,768$ cells per chunk) would bloat memory and destroy the cache efficiency of the pure-C core. Instead, NeverDark uses the **Hybrid $H + C$ Analytical Model**:

```
                              Z (Altitude)
                               ▲
          Sky / Open Air       │   Density < 0 (Air)
       ────────────────────────┼──────────────────────── Surface H(x, y)
          Soil / Surface Rock  │   Density > 0 (Solid)
       ───────┐        ┌───────┼────────────────────────
              │ Cavern │       │   3D Cave Field C(x, y, z) < CAVE_THRESHOLD
              │ Hollow │       │   Density < 0 (Open Cavern Room!)
       ───────┘        └───────┼────────────────────────
          Deep Bedrock         │   Density > 0 (Solid)
                               ▼
```

### 1. Surface Heightfield $H(x, y, w)$
$$H(x, y, w) = H_{\text{base}} + \sum_{i=0}^{N-1} A_i \cdot \text{VNoise}_{2D}(x \cdot 2^{-s_i}, y \cdot 2^{-s_i}, \text{seed}_i)$$
- **Analytical & Continuous**: Evaluated on the fly in fixed-point integer math (zero heap allocations).
- **Surface Elevation**: $h = H(x, y, w)$.
- **Sea Level / Water Body**: $W(w) = \text{water\_level}(w)$.
  - $h < W$: Ocean, lake, or submerged river bed. Depth is $W - h$.
  - $h \ge W$: Dry land with elevation $h - W$.
- **Slope & Normals**: $\nabla H = \left(\frac{\partial H}{\partial x}, \frac{\partial H}{\partial y}\right)$.
  - Gentle slope ($|\nabla H| \le \text{climb\_threshold}$): walkable terrain.
  - Steep gradient ($|\nabla H| > \text{climb\_threshold}$): impassable cliff, rock wall, or climbing challenge.
- **Performance**: $O(1)$ point evaluation taking $15\text{–}30$ nanoseconds.

### 2. Subterranean 3D Cavity Field $C(x, y, z, w)$
Evaluated **only** when querying underground coordinates ($z \le H(x, y, w)$) or searching for surface cave entrances:
$$C(x, y, z, w) = |\text{VNoise}_{3D}(x, y, z, \text{seed}_1) - 128| + |\text{VNoise}_{3D}(x, y, z, \text{seed}_2) - 128|$$
- **Solid vs. Air Evaluation**:
  ```c
  int terrain_solid(int16_t x, int16_t y, int16_t z, uint16_t w) {
      int16_t h = terrain_height(x, y, w);
      if (z > h)
          return 0; /* Open air above ground */
      if (z == h)
          return 1; /* Surface turf/rock */
      /* Below surface: test 3D subterranean cavity field */
      return terrain_cavity_3d(x, y, z, w) >= CAVE_THRESHOLD;
  }
  ```
- **Natural Cave Entrances**:
  Where a subterranean cavern tunnel breaches the surface ($z = H(x, y, w)$ and $C < \text{CAVE\_THRESHOLD}$), a cave entrance, sinkhole, or grotto naturally appears. The engine automatically exposes exit `E_DOWN` / `'<'`.

---

## 4. Multi-Scale Layering: One World, Multiple Consumers

| Consumer | Coordinate Resolution | What It Evaluates | Output & Behaviors |
|---|---|---|---|
| **Fast MUD Core (`axil-nd`)** | Integer room grid $(x, y, z)$ | $H(x, y)$, $C(x, y, z)$ | Automatic elevation tracking, exit computation (`E_UP`/`E_DOWN` on slopes and cave entrances), ASCII glyphs (`^` peak, `~` water, `<` cave entrance, `#` cavern wall, `.` cavern floor). |
| **Seven 2D Stage (`mods/nd/ux`)** | Fixed-point sub-tile $(wx, wy)$ | $H(wx, wy)$ continuous | Organic coastlines derived directly from $\{H = W\}$ level-sets; natural cliff borders from $|\nabla H|$; zero artificial tile-box warping. |
| **Future 3D Renderer (`liblamina` / WebGL)** | Float/Fixed-point $(x, y, z)$ | $H(x, y)$, $C(x, y, z)$ | 3D surface mesh vertices ($z = H(x, y)$), water surface planes ($z = W$), subterranean cavern meshes (marching cubes or voxel blocks). |

---

## 5. Implementation Roadmap & Milestones

### Phase 1: Canonical Pure-C `<nd/terrain.h>` Specification (Current)
- Author a standalone, zero-dependency header `<nd/terrain.h>` (usable in native `.so`, WASM, and unit tests).
- Implement:
  - 3D lattice hash function (`terrain_mix_3d`).
  - Fixed-point 2D and 3D value noise with smoothstep interpolation.
  - Multi-octave 2D surface heightfield $H(x, y, w)$.
  - Analytical surface normal / slope computation $\nabla H(x, y, w)$.
  - Dual-octave 3D cavern cavity evaluation $C(x, y, z, w)$.
  - Climate and biome resolution $B(H, T, R)$.
  - Unified point query `terrain_point(x, y, z, w)`.

### Phase 2: Standalone Unit Suite & Benchmarks (`tests/unit/test_terrain_3d.c`)
- Test continuity across chunk boundaries (zero tears or discontinuities).
- Test elevation boundaries, water thresholds, and mountain slopes.
- Test 3D cavern hollow connectivity, solid rock containment, and natural surface entrances.
- Benchmark query throughput to verify $> 10{,}000{,}000$ point evaluations/second.

### Phase 3: Engine Core Unshackling (`external/nd-world`)
- Update `struct bio` (`nd/world-types.h`) and `view_tile_t` to include `int16_t elevation` and `int16_t water_level`.
- Remove the `pos[2] == 0` check in `noise.c` and `spacetime.c`, allowing full 3D coordinate traversal.
- Implement slope traversal: walking onto adjacent cells with $\Delta H \le \text{step\_max}$ naturally adjusts player altitude $z$.
- Expose `E_DOWN` and `E_UP` dynamically at cave entrances and cavern tunnels.

### Phase 4: Network Protocol & BCP Adaptation (`external/axil-nd`)
- Preserve height information in `nd_map_tile_t` (or pack relative elevation $\Delta z$ into tile data).
- Allow the client to receive either explicit tile elevations or query the canonical `<nd/terrain.h>` engine directly using the world seed.

### Phase 5: Seven 2D Renderer Convergence (`external/nd-seven`)
- Refactor Seven's ground blending: replace artificial square tile-box warping with direct sampling of the canonical continuous elevation $H(wx, wy)$ and coastline $\{H = W\}$.
- Render cliff edges and grotto entrances directly from canonical elevation gradients.

### Phase 6: 3D NeverDark Client Foundation
- Author a reference 3D terrain mesh / chunk generator using `liblamina` or WebGL.
- Extract heightmap vertices directly from $H(x, y)$ and cavern walls from $C(x, y, z)$.
