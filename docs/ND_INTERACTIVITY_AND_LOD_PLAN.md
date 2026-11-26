# NeverDark Interactivity, Native Threading, & Pop-Free LOD Fix Plan

Roadmap for eliminating startup hangs, activating `libstele` native multi-threading, keeping camera-distance LOD smooth and cache-hot across 360° rotation, and integrating `apps/nd-native` into the main build.

---

## Phase 1 — Main Build Integration & Native Test Hygiene
*Goal: `apps/nd-native` builds as part of default `make` and never drifts silently again.*

1. **Wire `apps/nd-native` into root `Makefile`**:
   - Ensure `lamina-lib` and a new `nd-native` target (`$(MAKE) -C apps/nd-native`) are part of `all:` (after `mods`, since `mods/nd` generates `.cx.c` files).
   - Ensure `apps/nd-native` is cleaned under `make clean`.
2. **Fix `lib/bud-lamina/test_ws_native.c` timeout**:
   - Investigate and fix the hang in `./test_ws_native` so `make -C lib/bud-lamina test` passes fast and deterministically.
- **Verify**: `make` builds `apps/nd-native/nd-native` (`exit 0`); `make -C lib/bud-lamina test` passes in `< 2s`.

---

## Phase 2 — Non-Blocking Startup & Progressive First Paint (Web + Native)
*Goal: Game window/canvas stays 60 FPS interactive from Frame 0 even on low-end GPUs while shaders, LUTs, and chunks load in the background.*

### 2A. Remove Synchronous Driver Stalls in Native GL (`external/liblamina/src/webgl_native.c`)
- Detect `GL_ARB_parallel_shader_compile` / `GL_KHR_parallel_shader_compile` on context init and enable parallel compiler threads (`glMaxShaderCompilerThreadsARB`/`KHR`).
- Remove immediate `glGetShaderiv(..., GL_COMPILE_STATUS)` and `glGetProgramiv(..., GL_LINK_STATUS)` from `lam_glCompileShader()` / `lam_glLinkProgram()`.
- Implement real non-blocking `GL_COMPLETION_STATUS_KHR` in `lam_glGetProgramParameter()` and log compile/link failures lazily only after completion.
- Fix `translate_es300_to_gl330()` to strip `precision ...;` up to `;` instead of dropping the entire line, so both minified and unminified shaders translate cleanly.
- **Verify**: `make lamina-lib`, `tests/unit/run-test-lamina-bridge.sh`, and native GL compile/link unit test pass with zero premature stalls.

### 2B. Progressive 3-Tier Shader Boot & Staggered Queueing (`mods/nd/seven/nd_seven_gl.c`)
- Replace the binary all-13-Tier-A `pipelines_ready` gate with per-pipeline readiness + staggered submission (1–2 shaders queued per frame when parallel shader compile isn't available):
  - **Boot Tier**: `sky` + `terrain_dry_near` (plus minimal blit/post). As soon as these link, 3D rendering and full camera/HUD/input interactivity begin immediately.
  - **Scene Tier (streamed live)**: `terrain_seabed_near`, `water`, `billboard`, `mesh3d_opaque`, `mesh3d`, `grass`, `post`. Each pass checks `pipe.prog != 0` and activates cleanly as soon as it links.
  - **Polish & Macro Tier (existing Tier B)**: `terrain_dry_csm`, `seabed_mid`, `macro`, `macro_far`, `csm`, `terrain_shadow`, `mesh3d_shadow_*`, `clouds`, `ssao`, `bloom`.
- Expose `nd_seven_shader_progress(int *ready, int *total)` for optional non-blocking HUD loading feedback.

### 2C. Eliminate Frame-0 Synchronous CPU Bursts (`mods/nd/seven/nd_seven.c`, `nd_seven_gl.c`)
- Cap synchronous Frame-0 `instant_jump` chunk population to a small coarse root ring (`<= 8` chunks) so terrain appears immediately while finer levels stream asynchronously.
- Initialize `uNoise2DTex` / `uNoise3DTex` with 1×1 neutral placeholders on Frame 0 and bake the full LUTs asynchronously (via `stl_wq` or time-sliced cooperative slices on single-threaded WASM), uploading once ready.
- **Verify**: Time to first interactive frame `< 500 ms` with no main-thread freeze during loading.

---

## Phase 3 — Activate `libstele` Multi-Threading (`stl_wq_t`) Safely
*Goal: Real background CPU threads on native (`pthread`/Win32) and strict time-budgeted cooperative stepping on single-threaded WASM — zero main-thread spikes during terrain/foliage streaming.*

### 3A. Thread-Safety Audit & Per-Thread Memoization (`external/nd-world`, `mods/nd/seven`, `external/libareole`)
- Audit all static/global state touched in `ar_async_populate_worker` (`nd_field_sample_*`, `ND_FIELD_MEMO_EXTERN`) and `seven_async_block_bb_worker` (`s_border`, `nd_seven_warp_grid`, scratch buffers); make thread-local (`STL_THREAD_LOCAL`) or stack-local so concurrent workers are race-free.
- **Verify (TDD)**: Standalone multi-threaded determinism test comparing 1-thread vs 4-thread `ar_system` + `nd_field_sample` outputs bit-for-bit under sanitizers.

### 3B. Wire `s_seven_wq` & `cfg.wq` + Bounded GL Uploads (`mods/nd/seven/nd_seven.c`, `nd_seven_gl.c`)
- Initialize `s_seven_wq = stl_wq_create("seven", 0, 256)` on startup and destroy on teardown.
- Pass `cfg.wq = s_seven_wq` into `ar_system_create_ex(&cfg)` so `libareole` populates chunks in the background while rendering cached parent fallbacks (`AR_NODE_FLAG_DEFERRED`).
- Cap main-thread GL chunk texture uploads per frame (`<= 4` chunks/frame) and offload derived height/river CPU bakes where safe.
- Step `stl_wq_step(s_seven_wq, budget)` cooperatively when `nthreads == 0` (single-threaded WASM).
- **Verify**: `run-test-seven-render.sh`, `run-test-seven-perf.sh`, and `nd-native` confirm background workers active and smooth frame times.

---

## Phase 4 — Pop-Free Camera-Distance LOD & Zero Rotation Churn
*Goal: Preserve camera-distance LOD while making 360° rotation cache-hot and every LOD transition smooth with zero "jumping out."*

### 4A. Continuous CDLOD Vertex Geomorphing (Zero Extra Buffer Bandwidth)
- Reuse the 3 unused floats in `gl_chunk_inst_t.a_chunk_meta.yzw` (`48 bytes`, `3 × vec4`):
  - `a_chunk_meta.y` = `morph_start_dist`
  - `a_chunk_meta.z` = `morph_inv_range`
  - `a_chunk_meta.w` = `reveal_morph` (`1.0 → 0.0` ease-out over `~0.25s` when a newly populated finer child replaces its coarse ancestor)
- In the terrain (and water) vertex shaders, compute `morph = clamp(max((vDist - a_chunk_meta.y) * a_chunk_meta.z, a_chunk_meta.w), 0.0, 1.0)` and interpolate odd vertices `(2i+1, 2j)`, `(2i, 2j+1)`, `(2i+1, 2j+1)` toward their exact `L+1` parent-edge/diagonal midpoint sampled from `uHeightTex`.
- At `morph == 1.0`, the child mesh matches its parent lattice identically — eliminating vertical height pops and silhouette snaps.

### 4B. Stop Repeated Re-Refinement & Cache Churn on 360° Rotation (`external/libareole/src/ar_solver.c`, `mods/nd/seven/nd_seven.c`, `nd_seven_gl.c`)
- Add grace-period hysteresis memory (`last_active_tick` per `ar_node_t`) so brief off-frustum/occluded rotation does not immediately wipe split hysteresis; widen `split_hysteresis` (`0.15f → 0.25f`).
- Right-size caches for 360° orbit: raise `samples_pool_capacity` (`64 → 256`), `pool_capacity` (`512 → 768`), and `MAX_CHUNK_TEXTURES` (`256 → 512`) so a full 360° camera rotation stays cache-hot without eviction churn.
- Add hysteresis on `grid_lod` switches (`48×48 ↔ 24×24 ↔ 16×16`) in `nd_seven_gl.c`.

### 4C. Smooth Prop / Foliage / Billboard Transitions (`mods/nd/seven/nd_seven.c`, `nd_seven_gl_billboard.c`)
- Keep prop elevation anchored to canonical world height so props do not jump vertically when underlying terrain LOD changes.
- Add per-block `fade_in` (`0.0 → 1.0` over `~0.18s`) when an 8×8 foliage/prop block finishes baking so foliage glides in smoothly.

---

## Phase 5 — Verification & Benchmarks (Web + Native)
- Unit & sanitizer suites: `make stele-lib && make areole-lib && make standalone-unit-tests`, `make -C lib/bud-lamina test`, `run-test-seven-render.sh`, `run-test-viewport-registry.sh`, `run-test-seven-perf.sh`, `make test-fast`, `make test-mod MOD=nd`.
- Full build verification: `make` (including `apps/nd-native` and `site-wasm`), boundary checks (`check-module-boundaries.sh`, `check-wasm-imports.sh`, `check-no-site-specific-js.sh`).
- Real-world interactivity & 360° rotation verification on both native and browser.
