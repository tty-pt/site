PROMPT: Create ONE SINGLE, botanically accurate, mature `<SPECIES_NAME>` tree as a game sprite (along with its matching tangent-space normal map, if requested).
I am providing a reference image containing pixel-art lamb sprites. Use that image as the authoritative reference for the visual style, camera viewing angle, and pixel scale.

1. Botanical identity & online research
The tree must represent `<SPECIES_NAME>`.
Before drawing, search online for authoritative botanical information and reference photographs of `<SPECIES_NAME>` to determine its true morphology and mature dimensions:

- Look up the typical mature height range and crown spread of `<SPECIES_NAME>` in metres.
- Identify its characteristic growth habit and silhouette (e.g., canopy shape, crown density, height-to-width ratio).
- Identify its trunk and branching structure (e.g., trunk height and thickness, low vs. high branching, branch angles, bark colour and texture on young vs. old growth, root flare, presence of thorns/aerial roots/buttresses).
- Identify its foliage characteristics (e.g., leaf type, leaflet or needle arrangement, clustering pattern, seasonal/natural colouration, and any distinctive flowers, pods, cones, or fruit if characteristic).
- Do not draw a generic cartoon tree. The morphology, proportions, and colouration must be recognisably faithful to `<SPECIES_NAME>`.

2. Camera perspective & 3/4 top-down oblique projection — CRITICAL
Match the exact 16-bit top-down oblique (3/4 RPG diorama) perspective of the supplied lamb sprites (camera elevated at a ~55° pitch angle from horizontal, positioned to the south and looking down and north):

- Do NOT draw a flat side-on (0° platformer) elevation, and do NOT draw a pure 90° vertical bird's-eye view.
- Crown perspective: Because the camera looks down at a ~55° angle, the broad upper surface (top plane) of the crown must be tilted toward the viewer, showing full front-to-back depth across overlapping foliage tiers. The shaded underside of the front canopy rim and natural interior gaps should remain visible above the branches.
- Trunk & branch perspective: The trunk and ascending branches are seen in slight 3/4 foreshortening rising into the underside of the crown, with lower branches visible beneath the front foliage pads and through natural gaps in the canopy.
- Ground contact & root base: The base of the trunk and root flare must sit naturally on the tilted ground plane (with a rounded 3/4 footprint rather than a flat horizontal cut-off line), anchored at the bottom-centre of the sprite.

3. Absolute scale & lamb-relative math — CRITICAL
The reference image contains upright lamb sprites approximately 18 pixels tall.
Assume that each upright lamb is exactly 0.4 metres (40 centimetres) tall in the real world.
This establishes the fixed world-to-pixel scale:
- 1 lamb = 0.4 metres = 18 pixels
- 1 metre = 2.5 lambs = 45 pixels

Perform the following calculation using the researched mature height ($H$ in metres) of `<SPECIES_NAME>`:
1. Choose a representative mature specimen height $H$ (in metres) based on your online botanical research for `<SPECIES_NAME>`.
2. Compute the height in lamb units:
   $$\text{Lamb Multiplier} = \frac{H}{0.4\text{ m}} = H \times 2.5$$
3. Compute the exact vertical pixel height of the tree:
   $$\text{Tree Height (px)} = \text{round}(H \times 45\text{ px/m}) = \text{round}(\text{Lamb Multiplier} \times 18\text{ px})$$
4. Compute the target pixel width from the researched mature crown spread ($W$ in metres) using the same scale ($W \times 45\text{ px/m}$).

THE TREE ARTWORK MUST MEASURE EXACTLY THIS CALCULATED PIXEL HEIGHT VERTICALLY.
Measure vertically in canvas pixels from the bottom of its trunk/root base at ground level to the highest foliage pixel at the back/top of the tilted crown.
In other words, the tree must be exactly $\text{Lamb Multiplier}$ times the pixel height of an upright lamb in the reference image.
Do not make the tree approximately the same size as the lamb. Do not interpret the reference image's overall canvas dimensions as the intended sprite dimensions.

4. Visual style & unlit diffuse lighting (3D Day/Night & Normal Map Ready) — CRITICAL
Reproduce the visual language of the supplied lamb sprites while preparing the asset for dynamic 3D terrain lighting and a day/night cycle:

- Authentic pixel art with consistent, clearly defined pixel clusters and discrete pixel-level details.
- Limited, natural colour palette appropriate to `<SPECIES_NAME>`.
- UNLIT FLAT-DIFFUSE (ALBEDO) SHADING: Because this sprite will be paired with a tangent-space normal map in a 3D world with moving day/night lighting, DO NOT bake directional sunlight or harsh cast shadows into the sprite (no baked top-left sunlit highlights or heavy lower-right directional shadows).
- AMBIENT OCCLUSION (AO) & TIER SEPARATION ONLY: Restrict shading solely to soft, omnidirectional ambient occlusion—darkening only deep interior canopy crevices, tightly overlapping leaf tiers, branch crotches, and the recessed underside of the crown where ambient sky light naturally struggles to reach. Use subtle local hue/value shifts between distinct foliage pads and bark ridges so the pixel clusters remain crisp at night while preventing dual-shadow conflicts when the 3D engine's sun or point lights move across the normal map.
- Similar degree of pixel detail and visual simplification as the reference sprites.
- No photographic textures, painterly rendering, vector-art appearance, smooth gradients, antialiasing, or artificial glow.

The tree should look like it belongs to the exact same game world as the lambs, using natural, slightly muted base colours faithful to the foliage, bark, and branches of `<SPECIES_NAME>`.

5. Composition & Chroma-Key Background
Create exactly ONE tree.
Show the complete tree, including its entire 3/4-tilted crown, branches, and trunk base.
The tree should occupy most of the available canvas, and its overall width must match its botanically researched crown-spread-to-height ratio.
Do not include:

- Multiple trees, sprite sheets, animation frames, or alternative views.
- Lambs, animals, or people.
- Grass tufts, turf blades, soil patches, or cast ground shadows around the roots (the trunk base must be clean bark against the chroma-key background so the 3D engine can render terrain and dynamic shadows beneath it).
- Landscape scenery, background objects, text, labels, or borders.
- Fake checkerboard transparency grids.

6. Output requirements (JPEG / Pure Magenta Chroma-Key)
Output the final unlit diffuse artwork image (and state your researched mature height $H$, crown spread $W$, lamb multiplier, and resulting pixel dimensions).
- BACKGROUND: Because the output will be saved/transmitted as a JPEG without an alpha channel, place the tree on a flat, solid, pure magenta chroma-key background (`#FF00FF` / RGB `255, 0, 255`).
- HARD EDGES: Preserve razor-sharp, 100% hard-aliased outer pixel edges against the `#FF00FF` magenta background. Never use antialiasing, feathering, or semi-transparent edge blending, as this causes pink halo artifacts when chroma-keyed in the game engine.
- NORMAL MAP COMPATIBILITY: Ensure the silhouette and internal foliage clusters have clear, well-defined structural volume so a pixel-aligned tangent-space normal map can be generated on the exact same `#FF00FF` magenta background.
- EXACT SCALE: The actual tree artwork, from ground level to its highest foliage pixel, must measure the exact calculated pixel height vertically. If necessary, generate at a larger resolution and perform a nearest-neighbour resize to the required pixel dimensions. Never use bilinear or bicubic interpolation, and do not achieve the required height merely by adding background padding.

7. Tangent-Space Normal Map Specification (When Generating the Paired `_n` Normal Map)
When generating or deriving the paired tangent-space normal map (`<var>_n.png`) for the diffuse sprite:
- PIXEL-EXACT REGISTRATION: Every non-background pixel must align 1:1 with the diffuse sprite silhouette. Background pixels outside the tree silhouette must remain pure `#FF00FF` magenta (or `RGBA(128, 128, 255, 0)` after chroma-key extraction) with zero antialiased edge blending.
- OPENGL / WEBGL RIGHT-HANDED TANGENT-SPACE ENCODING (`RGB` $\to$ unit normal $\vec{N} = \text{normalize}(\text{RGB} / 127.5 - 1.0)$):
  - Red ($R$, $+X$ = East / Right): `0` = facing hard left ($-X$), `128` = neutral centre, `255` = facing hard right ($+X$).
  - Green ($G$, $+Y$ = Screen-Up / Zenith-Tilted): `0` = facing downward ($-Y$, shaded underside of foliage overhangs and lower branch bellies), `128` = vertical face, `255` = facing upward ($+Y$, sunward top plane of the tilted 3/4 crown).
  - Blue ($B$, $+Z$ = Toward Viewer / Out of Screen): `128` to `255` ($N_z \in [0, +1]$ pointing toward the camera; flat neutral normal is `#8080FF` / `RGB(128, 128, 255)`).
- TWO-SCALE 3/4 VOLUMETRIC STRUCTURE (~55° Camera Pitch):
  - Combine (a) the macro 3/4 crown dome and cylindrical trunk curvature (left bark edge $R < 95$, centre trunk ridge $R \approx 128, B \approx 245$, right bark edge $R > 160$) with (b) meso-scale hemispherical puffs for each individual overlapping foliage pad and pixel-level bark fissures, so moving directional sunlight and point lights sculpt every leaf tier and trunk fork in 3D.

Final result: one isolated, mature, botanically accurate, unlit diffuse `<SPECIES_NAME>` game sprite on a solid `#FF00FF` magenta background in 3/4 top-down oblique (~55° pitch) perspective, scaled via the 18 px = 0.4 m lamb ratio, visually consistent with the supplied lamb sprites and ready for 3D normal-mapped day/night rendering.

SPECIES_NAME =

