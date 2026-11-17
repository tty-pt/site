# NeverDark Seven 3D Occlusion & Culling Architecture Plan (`OCCLUSION.md`)

## 1. Problem Diagnosis (The 5 Performance Bottlenecks)

1. **No Planetary Back-Face / Horizon Culling on the CPU**
   - Below `dist = 250,000` (`mods/nd/ux/nd_seven.c`), `ar_system_query_matrix` culls in a flat 2D tangent plane (`flat_mvp`) with a sagitta approximation (`sag_radius = r_planet`). At high altitudes (`dist = 15,000 .. 249,000`), the wide frustum fan pulls in chunks that have already wrapped past the planet's curved horizon limb.
   - At `dist >= 250,000` (and for the companion planet `alt_world`), `ar_system_query_matrix` switches to 3D (`use_3d_primary = true`), seeding root candidates across the entire planetary torus (`±half_gw × ±half_gh` in `external/libareole/src/areole.c`). Because the camera frustum encloses the whole planetary body, chunks on the back side of the planet and inside the far torus throat pass the 6-plane frustum test.
   - Result: The CPU generates noise heightfields for the back side of the planet, uploads their textures to WebGL, draws their terrain triangles, and queues cloud shells for the far side of the planet.

2. **Unconditional Cloud Queueing (Off-Screen, Back-of-Planet, and Clear-Sky Chunks)**
   - In `mods/nd/ux/nd_seven_gl.c` (`nd_seven_gl_flush_terrain_chunk`), a cloud shell draw call is appended to `s_gl.cloud_queue` for every single terrain chunk dispatched:
     - **Vertical Frustum Mismatch**: Terrain chunks are culled at ground elevation $z \in [-256, +320]$, whereas the cloud deck sits at $z \in [420, 2820]$ (and lifts up to $+16,000$ in orbit). Because the camera looks down at a $55^\circ$ pitch, foreground terrain chunks near the top/sides of the ground frustum have their elevated cloud shells completely outside the screen viewport.
     - **Back-of-Planet Cloud Passes**: For back-facing chunks, `s_fs_cloud` waits until the fragment shader (`if (vn <= 0.02) discard;`) to reject pixels on the back side of the planet. WebGL still binds textures, sets uniforms, transforms 289 vertices per chunk, rasterizes 512 triangles, and loses hardware Early-Z optimization.
     - **Clear-Sky Chunks**: ~65% of the planet is anticyclonic clear sky (`eval_synoptic_is_clear_sky`), yet full proxy meshes are submitted and rasterized for chunks with zero cloud density.

3. **Arbitrary Opaque Terrain Draw Order (Defeating Hardware Early-Z)**
   - `s_visible_leaves` are sorted by 2D distance to the ground focal point (`focal_x, focal_y`) rather than 3D distance to the tilted camera eye. Southern mountain chunks closer to the camera eye can be drawn after distant northern valleys, causing expensive terrain fragment shader overdraw.

4. **No Mountain-Ridge Horizon Occlusion on the CPU**
   - At $55^\circ$ camera pitch, foreground mountain ridges occlude distant valley chunks, water surfaces, and thousands of tree/grass billboards behind them. Currently, the CPU still synthesizes and streams all billboards in hidden valleys.

5. **Companion Planet (`alt_world`) Draws When Underneath Ground or Eclipsed**
   - In `nd_seven.c`, `alt_world` is tested only against the 6 camera frustum planes (`ar_frustum_test_box`). When the companion planet is in front of the camera's look direction but below the local ground horizon (when standing on the surface) or eclipsed behind the primary planet's solid body, the engine still queries `libareole` and renders up to 32 terrain chunks and 32 cloud shells for the hidden planet.

---

## 2. Staged Implementation Plan

Following **Abstraction-First Test-Driven Development & Integration Sequencing**, we implement and unit-test the generic geometric culling hook in `libareole` first, then wire each occlusion stage into `mods/nd/ux/nd_seven.c` and `mods/nd/ux/nd_seven_gl.c`.

### Phase 1: `libareole` 3D Normal-Cone / Horizon Culling (`external/libareole`)
**Goal**: Stop `ar_system_query_matrix` from seeding, subdividing, or populating quadtree nodes on the back side of curved 3D manifolds before noise sampling ever happens.

1. **Extend `ar_cull_opts_t` (`external/libareole/include/ttypt/areole.h`)**:
   - Add an optional 3D manifold visibility/culling callback to `ar_cull_opts_t` and `ar_config_t`:
     ```c
     typedef bool (*ar_cull_3d_fn)(void *bounds_ctx, float ox, float oy, float w, float h,
                                   float z_min, float z_max,
                                   const float center_xyz[3], float span_w, float span_h);
     ```
   - Keeps `libareole` 100% domain-agnostic while allowing callers to reject back-facing manifold patches in $O(1)$ before `ar_get_or_populate`.
2. **Integrate into `ar_system_query_matrix` (`external/libareole/src/areole.c`)**:
   - In both the **Root Seeding Loop** and the **Priority Refinement Loop**, immediately after `ar_compute_node_3d_box` and `ar_frustum_test_box`, call `cull_3d_fn` if present.
   - If `cull_3d_fn` returns `false`, treat the candidate as culled (`visible = false`), preventing `ar_get_or_populate` from allocating or sampling noise for back-of-planet nodes.
3. **Standalone Unit Test (`external/libareole/tests/test_areole.c`)**:
   - Add unit test verifying that `cull_3d_fn` rejects back-hemisphere nodes, produces zero back-facing leaves, and avoids unnecessary node populations.
   - Verify with `make areole-lib && make standalone-unit-tests`.

---

### Phase 2: Planetary Horizon Culling & Companion Planet Occlusion (`mods/nd/ux/nd_seven.c`)
**Goal**: Reject all terrain chunks on the far side of the primary and companion planets, and skip `alt_world` entirely when it lies below the local ground horizon or behind the primary planet.

1. **Implement `seven_torus_cull_3d_cb` (`mods/nd/ux/nd_seven.c`)**:
   - Store `eye_3d[3]` and `cam_dist` inside `seven_bounds_3d_ctx_t`.
   - For a patch $[ox, ox+w] \times [oy, oy+h]$, compute angular spans $\Delta u = w / W_{\text{world}}$, $\Delta v = h / H_{\text{world}}$, and angular half-cone spread:
     $$\alpha = \pi \sqrt{(\Delta u)^2 + (\Delta v)^2}$$
   - Allow coarse root nodes with $\alpha \ge 1.05\text{ rad}$ ($\sim 60^\circ$) to subdivide until their normal cone is tight enough to test reliably.
   - Evaluate the patch center frame (`corolla_eval_frame` + `corolla_pose_point_to_inertial` / rotated normal $\vec{N}_c$) and view vector $\vec{V} = \text{normalize}(\vec{P}_{\text{eye}} - \vec{P}_c)$.
   - Reject the patch if:
     $$\vec{V} \cdot \vec{N}_c < -\sin(\alpha + \theta_{\text{relief}})$$
     where $\theta_{\text{relief}} \approx 0.10\text{ rad}$ safely accounts for mountain slopes and elevation relief.
2. **Horizon Limb Culling Across All High-Altitude Zooms (`nd_seven.c`)**:
   - Wire `cull_3d_fn = seven_torus_cull_3d_cb` into `opts` whenever `dist >= 8000.0f` (and in 3D mode), ensuring chunks wrapping past the curved planetary horizon are rejected even before `use_3d_primary` engages.
3. **Companion Planet (`alt_world`) Ground & Body Occlusion (`nd_seven.c`)**:
   - **Local Ground Horizon Occlusion (`dist < 35000.0f`)**: Evaluate the local surface normal $\vec{N}_{\text{local}}$ and camera eye $\vec{P}_{\text{eye}}$. Compute the vector to `alt_pdef.center` ($\vec{D} = \vec{C}_{\text{alt}} - \vec{P}_{\text{eye}}$). If the entire companion planet sphere of radius $R_{\text{alt}} = R_{\text{maj}} + r_{\text{min}} + 8000$ is below the local horizon tangent cone, skip `alt_world` in $O(1)$.
   - **Primary Planet Solid-Body Eclipse Check**: When in orbit, test whether the line of sight from `eye_3d` to `alt_pdef.center` is occluded by the primary planet's solid core sphere/tube.
   - Wire `alt_opts.cull_3d_fn = seven_torus_cull_3d_cb` so only the front hemisphere of `alt_world` is subdivided and rendered.

---

### Phase 3: Dedicated Cloud-Shell Culling (`mods/nd/ux/nd_seven_gl.c`)
**Goal**: Eliminate off-screen overhead clouds, back-of-planet cloud draw calls, and clear-sky (zero-density) cloud draw calls before they enter `s_gl.cloud_queue`.

In `nd_seven_gl_flush_terrain_chunk` (`mods/nd/ux/nd_seven_gl.c`), gate `s_gl.cloud_queue` insertion with `gl_should_queue_cloud_chunk`:

1. **Distance Gate**:
   - Immediately skip queueing if `s_gl.cur_dist <= 480.0f`.
2. **Cloud-Altitude 3D Frustum Test**:
   - Extract and cache the active 3D camera frustum planes in `s_gl` during `nd_seven_gl_begin_frame` (`ar_frustum_from_matrix` / Gribb-Hartmann planes from `s_gl.cur_mvp`).
   - Compute the 3D AABB of the chunk's elevated proxy shell at its true rendered altitude (`eff_shell_z + space_t * 14000.0f`, spanning $[z_{\text{shell}} - 250, z_{\text{shell}} + 450]$) using the 4 corners and center on the torus.
   - Test this elevated 3D AABB against the 6 frustum planes. Chunks whose ground footprint is visible at the top/edges of the screen but whose elevated cloud shell lies outside the camera frustum are rejected.
3. **CPU Planetary Horizon Limb Guard**:
   - Compute $\vec{V}_{\text{eye}} \cdot \vec{N}$ at the chunk corners and center in camera-relative 3D space.
   - If $\max(\vec{V}_{\text{eye}} \cdot \vec{N}) \le 0.015$, every pixel in the chunk would be discarded by `if (vn <= 0.02) discard;` in `s_fs_cloud`—skip queueing the chunk on the CPU.
4. **CPU Synoptic Clear-Sky Pre-Cull**:
   - Implement a fast pure-C helper `gl_chunk_is_synoptic_clear_sky(ox, oy, span_w, span_h, anim_time)` matching the shader's `eval_synoptic_is_clear_sky` (`0x53594E30u` and `0x53594E31u` 2D gradient noise octaves, including wind-shear offset margins).
   - For chunks up to regional scale (`span_w <= 65536`), sample a $3 \times 3$ lattice across the chunk (expanded by the max wind-shear displacement of $16,000\text{ wu}$). If all sample points lie well below the synoptic clear-sky threshold (`syn0 < 0.35f && syn1 < 0.33f`), skip queueing the cloud chunk.

---

### Phase 4: Front-to-Back Opaque Sorting & 1D CPU Ridge-Line Horizon Occlusion (`mods/nd/ux/nd_seven.c`)
**Goal**: Maximize hardware Early-Z rejection on opaque terrain and cull chunks + billboards hidden behind foreground mountain ridges.

1. **Front-to-Back Camera-Eye Sorting**:
   - Sort `s_visible_leaves` in `seven_dispatch_macro_backdrop` by 3D distance from the actual camera eye position `(cam_eye_x, cam_eye_y, cam_eye_z)` ascending, so foreground southern mountains always render into the depth buffer before background northern valleys.
2. **1D Screen-Space Ridge-Line Horizon Occlusion (`s_horizon_ndc_y[128]`)**:
   - In the diorama / low-altitude flight regime (`dist < 12000.0f` and `!use_3d_primary`), maintain a 128-column screen-space horizon array `horizon_y[128]` initialized to `-1.0f` (bottom of screen in NDC).
   - Iterate `s_visible_leaves` front-to-back:
     - **Visibility Test**: Project the chunk's maximum upper envelope (`z_top = (node->max_val / 16.0f) + 96.0f` for tallest trees/billboards) into NDC `[x_min..x_max, y_max]`. If the chunk is strictly in front of the camera (`w > 0`) and for every spanned column $c \in [c_0, c_1]$, `y_max + 0.015f < horizon_y[c]`, the chunk and all its billboards are 100% hidden behind a nearer mountain ridge:
       - Skip `nd_seven_gl_flush_terrain_chunk` for this chunk.
       - Remove the chunk from `s_visible_leaves` so `nd_seven_render` skips all room/billboard synthesis inside it.
     - **Horizon Update**: Sample interior ridge rows of `node->values` (taking conservative minimum elevation across each column bin segment) and project into NDC to raise `horizon_y[c]`.

---

### Phase 5: Verification & Quality Gates (Tier 0 & Tier 1)
1. **Core Library Gate**:
   - `make areole-lib && make standalone-unit-tests`
2. **Isomorphic Dual-Compile & Boundary Gate**:
   - `make -C mods/nd && make site-wasm`
   - `sh scripts/check-module-boundaries.sh && sh scripts/check-wasm-imports.sh`
3. **Targeted Seven & Planetary Render Verification**:
   - Run `tests/unit/test_seven_render`, `tests/unit/test_planet_zoom_coverage`, `tests/unit/test_torus_planetary_renderer`, and `tests/unit/test_ground_zoom_lod`.
   - Verify zero visual gaps on the visible hemisphere and confirm substantial reductions in active chunks, cloud queue count, and billboard synthesis.
