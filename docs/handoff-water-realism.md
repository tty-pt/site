# TASK HANDOFF: Realistic pure-C water for the NeverDark Seven stage (libnoise, world-anchored)

Status: ready to pick up. Prior work: noise was constant in WASM until commit
`bf3e372` (libnoise xxhash fix); the water animates now, but looks basic.

## 0. Scope

Make the Seven-stage water look **realistic**, the way water looks from above in
real life:

- thin, sharp caustic lines moving on the seabed;
- waves that travel and have visible height;
- foam that arrives in bands and pulls back;
- shallow water that changes from clear to turquoise to deep navy with depth.

Use smooth tones, not stylised ones. Touch only the water look; don't fix
unrelated issues. Don't move the player character: the headless browser loads
the user's live character. Work in a Quest Journal quest and commit when done.

## 1. Rules you must not break

1. **Same code on server and browser.** `mods/nd/ux/*` builds twice:
   - a native `.so` for server rendering on a headless OpenBSD VM (no GPU, no
     WebGL, no GLSL);
   - `htdocs/nd.wasm` for the browser.

   All water rendering is pure C in `mods/nd/ux/nd_seven_ground.c`.
2. **Noise comes from libnoise.** Use `external/libnoise/include/ttypt/noise.h`
   (`noise_point_2d/3d`: value noise, `int16_t` coordinates, results 0–255).
   The user insists on libnoise. If you need a new primitive, such as
   cellular/Voronoi noise, add it to libnoise with its own tests, not in place
   in the renderer.
3. **No screen-space effects.**
   - Keep the shader in `mods/nd/ux/nd_seven_shader.h` a passthrough.
   - No camera-dependent glints (`dot(N,H)^n`), no Fresnel, no viewport
     distortion.
   - Lighting that uses a *fixed world sun direction* (diffuse shading of wave
     slopes) is allowed, because it doesn't depend on the camera.
4. **Everything stays anchored to world coordinates.** Moving the camera must
   never move any water pattern.
5. **Time only moves forward.**
   - Every time input is an integer that only increases, derived from
     `nd_seven_get_anim()`.
   - No `%` cycles, no triangle waves, no sin/cos back-and-forth.
   - Wrapping an `int16_t` value is seamless *only* for power-of-two cell
     sizes, which is what libnoise uses.
   - Drifting a coordinate over time (for example `gx - (anim >> 2)`) is
     allowed and is how you make waves travel. It is seamless for the same
     reason.
6. **Rules for the UX code:**
   - No `#if` blocks that choose what gets drawn.
   - No `XY_`, `corm_`, `source_`, `axil_` or `hyle_source` identifiers in UX
     code.
   - No site-specific JavaScript.
7. **Do not touch the xxhash settings in `noise.h`** (`XXH_INLINE_ALL` plus
   `XXH_FORCE_MEMORY_ACCESS 3`, commit `bf3e372`). Without them, WASM noise
   silently becomes 0. `scripts/check-wasm-imports.sh` fails the build if
   `env.XXH*` comes back as a browser import.

## 2. Where things are (line numbers are approximate)

- **`mods/nd/ux/nd_seven_ground.c`:**
  - **Per-frame water overlay:** the `if (chunk->depth)` branch of
    `nd_seven_ground_tile_render()`, around lines 980–1066. It runs every frame
    and reads `chunk->pixels`, the cached substrate with water already
    composited.
  - **Depth map:** `chunk->depth[y*TILE_PX_W+x]` per pixel, 1–28 for water, 0
    for land.
  - **Depth is written in** `render_water_cell()` (~278) and in the border
    blend `depth_grid` (~590). `nd_water_depth_at()` is at ~258.
  - **Water colour:** `water_color_at_depth()` (~41) and
    `water_alpha_at_depth()` (~64, a Beer–Lambert lookup table, one alpha for
    all channels).
  - **World position:** `base_gx = pos[0]*NDW_TILE_W`,
    `base_gy = -pos[1]*NDW_TILE_H`, `gx = base_gx + x_in_tile/SCALE`, with
    `SCALE = 2`, so one world unit is 2 px.
  - **Cost-saving caches:** noise is only re-sampled when `gx` changes
    (`last_gx_*` variables).
  - **Chunk lifetime:** chunks are built once by `chunk_get_or_create()` and
    evicted by position. The replace-at-same-position path (~770) frees
    `pixels` but leaks `depth`; fix it if you add buffers to chunks.
- **Clock:**
  - `nd_seven_motion.c:504` increments `s_anim` once per `wasm_tick` (about 60
    per second).
  - `nd_ui.c` `wasm_tick` (~783–817) redraws the stage every 2 ticks, so
    `anim` advances by 2 per drawn frame.
  - `seven_stage_redraw()` still pushes uniforms to the passthrough shader;
    that's harmless.
- **Ponds:** pond scenery objects are drawn separately by `draw_pond()` in
  `mods/nd/ux/nd_seven_atlas_draw.c:364`. Larger ponds made of water tiles go
  through the ground-water path. Decide whether pond objects also get the new
  look.
- **Biome edges** use nd-seven's own noise (`ndw_vnoise` in
  `external/nd-seven/include/nd/seven-world.h`, with its own `ndw_mix` hash),
  not libnoise. Leave them alone.
- **Tests:**
  - `tests/unit/test_seven_world.c` includes a water-visibility and anchoring
    test (`b3bc4af`).
  - Run with `bash tests/unit/run-test-seven-world.sh`,
    `bash tests/unit/run-test-seven-render.sh` and
    `make -C external/libnoise test`.

## 3. Why it looks basic now

- **Caustics are blobs.** The current rule, `abs(c1-c2) < 22` on two coarse
  noise layers, makes soft patches, not thin lines.
- **Swell is flat.** It's lighter and darker patches, with no travel
  direction, no slope shading and no sense of height.
- **Foam is a static white strip.** The `d == 1` band has a little noise but
  doesn't move toward the shore.
- **Colour is grey-scaled.** One alpha for all channels, so shallow water
  doesn't turn turquoise and deep water doesn't lose red first.
- **Wrong order.** Caustics are blended *after* the water tint, but physically
  they light the seabed and are then tinted by the water above.
- **Too coarse.** Everything is sampled once per world unit (2 px), so lines
  can't be thinner than 2 px.

## 4. Design to implement (libnoise only)

Build each piece behind a small `static` helper and check it on still frames
before combining.

1. **Caustic lines (d = 2–6).**
   - Sample at pixel resolution: `px = base_gx*SCALE + x_in_tile`,
     `py = base_gy*SCALE + y_in_tile`.
   - Bend the pattern so lines curve: `wx = noise3(px,py,t,SEED_W,5) - 128`,
     `u = px + wx/4`, and the same for `v`.
   - Thin lines where two layers meet:
     `e = abs(noise3(u,v,t,S1,4) - noise3(u+37,v-23,t2,S2,4))`, then
     `L = max(0, K - e)`. Square or cube `L` to make the lines thinner and
     brighter.
   - Use two layers at different scales and speeds and take the brighter one,
     so lines form a connected web.
   - Deeper water means wider, dimmer lines: fade with the light transmission
     at `d` and widen `K` with depth.
   - Apply caustic light to the seabed colour *before* the water tint. That
     requires keeping the substrate separately in the chunk
     (`chunk->substrate`, plus the water parameters or `d`) and compositing per
     frame.
   - Measure the extra memory (TILE_PX_W × TILE_PX_H × 4 bytes per chunk) and
     the speed.
2. **Travelling waves (d ≥ 3).**
   - Height `h(x,y,t)` is 2–3 noise layers whose coordinates drift along a
     fixed wind direction over time.
   - Shade by slope: work out the slope from neighbouring samples
     (`h(x+1)-h(x-1)`, `h(y+1)-h(y-1)`) and light it with a fixed sun
     direction. Brightness = ambient + k·max(0, N·L). This is diffuse
     lighting, not a glint.
   - Optionally make crests sharper and troughs broader (fold or square `h`)
     so they read as ocean swell.
3. **Shore.**
   - Foam bands move shoreward: `phase = dist_to_land * A + (anim >> s)`,
     broken up with noise, drawn only where the band and the noise line up.
   - Spread the swash over `d = 1–3`, not just a flat strip at `d == 1`.
   - Wet sand next to the water darkens. This needs distance information for
     land pixels, which the depth map doesn't give today (land is `d = 0`).
4. **Colour by channel.**
   - Replace the single alpha with per-channel absorption, red fading fastest:
     shallow water over sand reads turquoise, deep water navy.
   - Keep the lookup-table approach (`s_beer_lambert_alpha`), one table per
     channel.
5. **Smooth, high-fidelity output (decided: realistic).**
   - **No banding:** no colour quantisation, no Bayer dither, no reduced
     palettes for water.
   - **Fine detail:** water detail is computed at pixel resolution wherever it
     shows: caustics, foam edges and slope shading. Per-world-unit caching
     with bilinear interpolation is allowed only for the broad swell height
     field.
   - **Blend carefully:** blend in roughly linear light. Square the colours
     before mixing and take the square root after, or use a 256-entry lookup
     table. This stops bright caustics and foam looking grey and muddy.
   - **Light model, fixed world sun:**
     1. Seabed colour × (ambient + caustic light).
     2. Per-channel absorption by depth `d`.
     3. Light coming up from inside the water (blue-green, more for deeper
        water).
     4. Diffuse slope shading of the surface.
     5. Foam.
   - **Seabed visible in shallows:** at `d` 1–3 the substrate texture (sand
     grain, rocks) must stay visible through the water.
   - **Edges:** a soft wet edge where water meets land, and no hard step
     between depth levels. Smooth `d` per pixel, or interpolate between the
     depth-table values.
6. **Speed.**
   - Noise is the expensive part: each `noise_point_3d` call hashes 8 points.
   - Realism takes priority, but the stage must still reach ≥ 30 drawn frames
     per second in headless Chrome at 900×656.
   - If it doesn't, first cut wasted work: skip land pixels, compute caustics
     only where `d` is 2–6, and cache the swell per world unit.
   - Only then lower resolution, and for the swell only, never for caustics or
     foam.
   - Report ms per frame before and after.

## 5. Verification (all required)

1. **Measure the starting point first.**
   - Time one stage redraw in WASM. Use the node harness (step 5.4) with
     `performance.now()` around `wasm_tick`, and in Chrome a flush-rate
     measurement.
   - Record ms per frame at 900×656, before and after.
2. **Unit tests** in `tests/unit/test_seven_world.c` (write them first; they
   must fail before the change):
   - Anchoring: moving the camera keeps the pattern fixed.
   - Time keeps moving forward: no repeat between frames t and t+2k for k up
     to 64.
   - Caustic coverage: bright caustic pixels make up 5–25% of the `d` 2–6
     pixels.
   - Thin lines: the average horizontal run of bright pixels is ≤ 4 px.
   - Swell: the travel direction matches the wind vector, checked by
     correlating frames that are a few ticks apart.
   - No banding: on the deep-sea test tile, the brightness histogram of water
     pixels has ≥ 24 distinct levels.
   - Shallows show the seabed: substrate contrast survives at `d` 1–2, with at
     least 50% of the dry-sand variance.
   - Server/browser parity: the native render and the WASM render of the same
     state give the same pixels.
3. **Headless Chrome.**
   - Load Playwright from
     `node_modules/.deno/playwright@1.60.0/node_modules/playwright`, using
     `executablePath: "/usr/bin/google-chrome"` and the args
     `--use-gl=swiftshader --enable-unsafe-swiftshader --ignore-gpu-blocklist`.
   - Load `http://localhost:8080/nd`.
   - Hook `WebGLRenderingContext.prototype.texImage2D` with `addInitScript` to
     hash the uploaded frames for canvas `#seven-stage-canvas`. Expect many
     distinct hashes per second.
   - Take element screenshots of the canvas 1.5 s apart and compare them with
     `magick compare -metric AE -fuzz 8%`.
4. **Node replay harness, for repeatable rooms.**
   - Capture WebSocket frames with Playwright's `page.on("websocket")`, and the
     `#bud-state` JSON.
   - Instantiate `htdocs/nd.wasm`. Fill missing imports with stubs, but
     **never provide `XXH32`**.
   - Shim WASI `fd_write` so temporary `fprintf(stderr, …)` output shows up.
   - Call `wasm_init(state)`, then `wasm_ws_on_channel(ch, ptr, len)` per
     captured frame (`ch=1` when bytes start `#b`, `2` for `#r`, else `0`),
     then `wasm_tick(1/60, 900, 656)` N times.
   - Capture frames from `lamina.lam_canvas_flush(ptr, w, h)`.
   - `make -C mods/nd` rebuilds only `nd.wasm`, in about 7 s.
   - For the procedural sea (no map), set `"preferred_driver":"lamina-seven"`
     in the state JSON.
5. **Realism check by the user.**
   - Produce three side-by-side reference frames and a 3–5 s animated WebP for
     each scene: tropical shallows over sand, open deep sea, and the pond room.
   - Present them with `quest_ask_human`, asking "realistic or not, and what's
     off?", and keep iterating until the user approves.
   - A passing metric is not a pass; the user's eye decides.
6. **Final checks:**
   - Run the boundary scripts `check-module-boundaries`, `check-ux-purity`,
     `check-no-site-specific-js` and `check-wasm-imports`.
   - Confirm with `wasm-objdump -j Import -x htdocs/nd.wasm` that no new
     `env.*` imports appeared.
   - Run the full `make`, and remove all debug output before committing.

## 6. Lessons from earlier attempts

- A harness that supplies its own versions of missing imports can hide bugs,
  because the browser fills them with functions that return 0. Always check
  the import list.
- Following a step to the letter removed the visible motion. Check that
  nothing the user can see disappears.
- "Unit test passes" is not the same as "visible in the browser". Verify frames
  in real Chrome, and check effect strength on actual frames, not by reasoning
  about numbers.
- Don't run filesystem-wide `find`; it's too slow.
