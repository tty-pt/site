# NeverDark Universe & Programmable World Architecture (`GAME_IDEA.md`)

This document captures the architectural vision, mathematical foundations, implemented design decisions, and remaining open explorations for the **NeverDark** universe, its programmable multi-world governance model in [`nd-region`](file:///home/quirinpa/site/external/nd-region/include/nd/regions.h), its optional 64-bit spatial indexing in [`libislet`](file:///home/quirinpa/site/external/libislet/include/ttypt/islet.h), and its standalone botanical 3D surface, rigid-body orbital, and irradiance library [`libcorolla`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h).

---

## 1. Core Vision: A Programmable Universe on a Lean OpenBSD Server

NeverDark is not a single static game world; it is a **programmable multi-world game engine** designed to run with minimal memory, disk, and CPU overhead on a resource-constrained **OpenBSD server**.

1. **Moderator Autonomy & Resource Slicing**:
   - The universe is partitioned into a binary hierarchy of prefixes. Moderators (and sub-moderators) are granted prefixes representing a fraction of the universe's keyspace and a corresponding fraction of server resources (storage/modify quotas).
   - Within their delegated slice, moderators are free to code and configure completely different kinds of worlds and gameplay rules without having arbitrary conventions forced upon them.
   - A moderator's slice can be a hard-sci-fi travelable 32-world solar system, a single toroidal or spherical planet, a deep 3D voxel-like underground mining/digging world, or a classic non-spatial MUCK text adventure with room-by-room chat.
2. **World 0 as the Flagship Reference Implementation**:
   - Rendering a continuous 3D planet for **World 0** is a deliberate starting choice that exemplifies the engine's built-in, high-efficiency defaults.
   - These defaults (procedural 3D manifolds, axial-tilt solar climate, Keplerian multi-body ephemerides, reversible terrain/resource modifications, radial outdoor chat) are provided as modular, reusable building blocks ([`libislet`](file:///home/quirinpa/site/external/libislet/include/ttypt/islet.h), [`libchaff`](file:///home/quirinpa/site/external/libchaff/include/ttypt/chaff.h), [`libareole`](file:///home/quirinpa/site/external/libareole/include/ttypt/areole.h), [`libcorolla`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h)) that any moderator can adopt, tune, or bypass.
3. **Zero Idle Overhead per World**:
   - Supporting at least $2^{16}$ ($65{,}536$) worlds—and potentially many more subdivisions—must cost **zero extra RAM, zero extra file descriptors, and zero disk space** for unmodified or unexplored regions.
   - Database storage scales strictly with **active player footprint and un-cooled modifications** ($O(\text{modified/claimed rooms})$), never with total world count or planetary surface area ($O(\text{world area})$).

---

## 2. Reversible Modifications & Zero-Storage Equilibrium

Players can interact with and modify the world—harvesting resources (e.g., gathering sticks from trees, mining stones) and modifying 3D terrain (digging trenches, carving tunnels, piling earth, building structures)—without bloating the [`libcorm`](file:///home/quirinpa/site/external/libcorm) database on OpenBSD.

### 2.1 Deterministic Baseline + Timestamped Deltas
- Every procedural room and its baseline elevation/biome/flora state is deterministically synthesized on the fly from the world's seed and coordinates via [`libchaff`](file:///home/quirinpa/site/external/libchaff/include/ttypt/chaff.h) and [`field.h`](file:///home/quirinpa/site/external/nd-world/include/nd/field.h).
- Unmodified procedural rooms carry the `RF_TEMP` flag and occupy **0 bytes** in persistent storage when unobserved.
- When a player modifies a room (chops branches, digs terrain), the room is marked `RF_MODIFIED` and stores a compact **timestamped delta** ($t_{\text{mod}}$) rather than requiring a background cron job to tick millions of rooms.

### 2.2 Lazy Cooldown & Natural Relaxation
Instead of polling modified rooms, relaxation back to the deterministic initial state is evaluated **lazily on read or during unobserved garbage-collection sweeps** ([`check_deterministic_recovery()`](file:///home/quirinpa/site/external/nd-world/src/spacetime.c#L599-L671) and [`eng_room_clean()`](file:///home/quirinpa/site/external/nd-world/src/spacetime.c#L674-L716)):
1. **Flora & Resource Regrowth**:
   - Harvested resources regenerate as a pure function of elapsed time $(t - t_{\text{mod}})$ and the world/biome regrowth cooldown $\tau_{\text{regrow}}$:
     $$\text{sticks}(t) = \min\!\left(\text{sticks}_{\text{initial}},\; \text{sticks}_{\text{remaining}} + \left\lfloor \frac{t - t_{\text{mod}}}{\tau_{\text{regrow}}} \right\rfloor\right)$$
2. **3D Terrain Digging & Weathering Cooldown**:
   - Casual digging or piling dirt records a sparse height/voxel delta $\Delta H(x, y, z)$ with timestamp $t_{\text{mod}}$.
   - Over an erosion/slumping cooldown $\tau_{\text{erosion}}$, unreinforced excavations weather back toward $\Delta H \to 0$.
3. **Zero-Memory Eviction**:
   - Once all objects and terrain deltas in an unobserved room (outside the active player observation horizon, [`is_room_observed()`](file:///home/quirinpa/site/external/nd-world/src/spacetime.c#L560-L596)) have relaxed back to their deterministic seed state, `RF_MODIFIED` is cleared and the room record is deleted (`eng_object_move(here_ref, NOTHING)`), returning its RAM and disk footprint to **0 bytes**.
4. **Bounded Permanent Modifications**:
   - Players cannot modify terrain permanently without bound. Permanent structures, shored-up tunnels, or claimed plots ([`carve()`](file:///home/quirinpa/site/external/nd-world/src/spacetime.c#L725-L785) / [`st_region_claim_room()`](file:///home/quirinpa/site/external/nd-region/src/libnd-region.c#L761-L802)) are subject to per-player and per-moderator entropy/storage quotas (`quota_rooms` / `used_rooms`).

---

## 3. The Unified 64-Bit Prefix Tree (`nd-region` & `libislet`)

### 3.1 Opaque 64-Bit Keys at the Base: Spatial Coordinates Are Optional
- At the lowest level of the engine, a room or domain address is simply an **opaque 64-bit integer (`uint64_t`)** governed by a prefix `(uint64_t prefix, uint8_t prefix_len)` of length $k \in [0, 64]$ in [`nd-region`](file:///home/quirinpa/site/external/nd-region/include/nd/region-types.h) (`struct st_rec`).
- **We assume as little as possible about how moderators code or structure their slice**:
  - A moderator is **never forced** to use Euclidean coordinates, Morton codes, or planetary surfaces.
  - **World 0 (and continuous spatial worlds)** opt into encoding the lower bits of their prefix as a 3D Morton coordinate via [`libislet`](file:///home/quirinpa/site/external/libislet/include/ttypt/islet.h) (e.g., the existing $(2^{16})^3$ `(x, y, z)` space per 16-bit `world_id`, or an asymmetric `(bits_x, bits_y, bits_z)` schedule).
  - **Non-spatial or custom moderators** can treat the $64 - k$ free bits beneath their prefix $k$ however they want: sequential room IDs (`prefix | 1`, `prefix | 2`, `prefix | 3`) connected by classic TinyMUCK exits, a roguelike `(dungeon_id, floor, room_idx)` bitfield, or starship deck/cabin numbers.
- Regardless of how a sub-moderator interprets the lower bits, **every key $K$ inside their jurisdiction still begins with the same $k$-bit prefix**:
  $$\text{Range}(P, k) = \left[\, P \ll (64 - k),\;\; ((P + 1) \ll (64 - k)) - 1 \,\right]$$
  That means prefix containment (`st_region_covers`), authority (`st_can_region`), bans (`eng_st_ban_check`), storage quotas, and **recursive moderator broadcasts (`wall`)** work identically across all prefixes without needing to decode or assume spatial coordinates.

### 3.2 One Continuous 64-Bit Binary Hierarchy (Universe $\to$ Worlds $\to$ Regions $\to$ Leaf Keys)
Instead of treating `world_id` and intra-world keys as disconnected systems, the entire 64-bit key is a **single continuous binary prefix tree (a 64-bit hierarchical trie)** from MSB (bit 63) down to LSB (bit 0). For spatial worlds using `libislet`:

$$\underbrace{\text{World Hierarchy Prefix}}_{\text{Top } 16\text{ bits (bits } 63..48\text{)}} \;\Vert\; \underbrace{\text{Intra-World Key (e.g., 3D Morton }(x, y, z)\text{ or Opaque ID)}}_{\text{Bottom } 48\text{ bits (bits } 47..0\text{)}} \quad = \quad 64\text{ bits (`uint64_t`)}$$

Every prefix `(uint64_t id, uint8_t plen)` of length $k \in [0, 64]$ addresses a contiguous subtree of the universe:

| Prefix Length $k$ | Fixed Bits | Free Bits | What the Prefix Addresses & Governs |
| :--- | :--- | :--- | :--- |
| **$k = 0$** | $0$ | $64$ | **The Entire Universe** ($2^{16} = 65{,}536$ worlds). A full range scan or `wall` reaches everyone in the cosmos. |
| **$k = 1$** | $1$ | $63$ | **Half of the Universe** ($2^{15} = 32{,}768$ worlds). |
| **$k = 2$** | $2$ | $62$ | **A Quarter of the Universe** ($2^{14} = 16{,}384$ worlds). |
| **$k = 4$** | $4$ | $60$ | **A 16th of the Universe** ($2^{12} = 4{,}096$ worlds)—e.g., a top-level moderator's travelable space-simulator sector. |
| **$k = 11$** | $11$ | $53$ | **A 32-World Solar System** ($2^5 = 32$ worlds). |
| **$k = 14$** | $14$ | $50$ | **4 Worlds** governed by the moderator of that 14-bit prefix. |
| **$k = 15$** | $15$ | $49$ | **2 Worlds** (e.g., a planet and its moon) governed by a 15-bit moderator. |
| **$k = 16$** | $16$ | $48$ | **1 Single World** (all 16 top bits fixed; 48 intra-world bits free). |
| **$k = 17$** | $17$ | $47$ | **Half of a World** (e.g., Eastern or Western hemisphere, or sub-domain `0`/`1`) delegated to a sub-moderator. |
| **$k = 18$** | $18$ | $46$ | **A Quarter ($1/4$) of a World** (a planetary quadrant). |
| **$k = 24$** | $24$ | $40$ | **A Continent / Province** ($1/256\text{th}$ of a world). |
| **$k = 36\text{–}44$** | $36\text{–}44$ | $28\text{–}20$ | **A City, Guild Claim, or Underground Dungeon Complex**. |
| **$k = 64$** | $64$ | $0$ | **A Single Leaf Room Key**. |

### 3.3 Why This Unifies Governance, Recursive `wall`, Quotas, and Optional Spatial Queries
1. **No Hard Wall Between "World Bits" and "Intra-World Bits"**:
   - The top 16 bits are the first 16 levels of a **single 64-bit binary subdivision tree** already backed by [`nd-region`](file:///home/quirinpa/site/external/nd-region/src/libnd-region.c) (`st_rec` rows keyed by `(id, plen)`).
   - All 16 bits fixed ($k = 16$) identify **1 world**; 15 bits fixed ($k = 15$) identify **2 worlds**; 14 bits fixed ($k = 14$) identify **4 worlds**; 11 bits fixed ($k = 11$) identify **32 worlds** (e.g., a solar system); 4 bits fixed ($k = 4$) identify **$4{,}096$ worlds ($1/16\text{th}$ of the universe)**; 2 bits ($k = 2$) identify **a quarter of the universe**; 1 bit ($k = 1$) identifies **half of the universe**; and 0 bits ($k = 0$) encompasses **the entire universe**.
   - That exact same binary subdivision continues seamlessly past bit 16 into a world's interior ($k = 17, 18, 24, \dots, 64$).
2. **Recursive Moderator `wall` Across All Subjects**:
   - Because any prefix $(P, k)$ covers every 64-bit location key $K$ satisfying `st_region_covers(P, k, K, 64)` (i.e., `((K ^ P) >> (64 - k)) == 0`), a moderator's **`wall` command** is naturally **recursive across their entire subtree**.
   - When a moderator of prefix $(P, k)$ runs `wall <message>`, the engine broadcasts to every connected player whose current 64-bit location key falls inside $[P \ll (64 - k),\; ((P + 1) \ll (64 - k)) - 1]$—automatically reaching players in all sub-regions and sub-worlds delegated to child moderators beneath $(P, k)$, regardless of whether those child regions use 3D Morton coordinates or non-spatial room graphs.
3. **Recursive Delegation Without Forcing Conventions**:
   - Any moderator holding a prefix $(P, k)$ can delegate $(P \Vert 0, k+1)$ or $(P \Vert 1, k+1)$ via `grant` in `nd-region`.
   - Crucially, **parent moderators do not force coordinate or gameplay conventions on their sub-moderators**: a moderator who owns a 32-world solar system ($k = 11$) can provide orbital positions in the shared skybox while letting sub-moderators of individual worlds ($k = 16$) or binary pairs ($k = 15$) freely choose whether their world is a toroidal planet, a spherical planet/moon, a $(2^{16})^3$ 3D voxel space, or a non-spatial MUCK station.
4. **Spatial Worlds & Optional Asymmetric 3D Allocation in `libislet`**:
   - Our existing $(2^{16})^3$ per-world 3D space (`pos_morton_encode` in `nd-region` / `libislet`) already maps `(world, x, y, z)` into the 64-bit key (`(uint64_t)world << 48 | morton3(x, y, z)`).
   - When `libislet` is extended with optional asymmetric bit schedules (`bits_x + bits_y + bits_z <= 48`), spatial worlds that want larger surfaces than $2^{16} \times 2^{16}$ can trade vertical $Z$ bits for horizontal $X, Y$ bits (e.g., `20, 20, 8` for $2^{20} \times 2^{20}$ surface rooms + $256$ vertical $Z$ layers) while keeping the base 64-bit key contract untouched.

---

## 4. Planetary Geometry, Scale & Why Projection Tricks Were Rejected

### 4.1 The Legacy Flat-Sun Planet 0 Problem (Now Solved)
From [`WORLD_MATH.md`](file:///home/quirinpa/site/WORLD_MATH.md#L154-L178), anchored to the anatomical scale of the pixel-art lamb (`NDS_LAMB` = $18\text{ wu}$ tall $\iff 0.72\text{ m}$ real-world `Ovis aries`, so **$1\text{ wu} = 4.0\text{ cm}$**):
- Planet 0 has a $2^{16} \times 2^{16}$ ($65{,}536 \times 65{,}536$) room lattice of $160 \times 128\text{ wu}$ ($6.40\text{ m} \times 5.12\text{ m}$) rooms.
- Its linear East–West width is $419.43\text{ km}$ ($\sim 1.05\%$ of Earth's $40{,}075\text{ km}$ circumference; outer equatorial circumference is $838.18\text{ km}$, or $\sim 2.09\%$ of Earth).
- Because surface area scales quadratically, its total 3D torus surface area ($R = 2{,}000{,}000\text{ wu} = 80\text{ km}$, $r_0 = 1{,}335{,}000\text{ wu} = 53.4\text{ km}$) is **$168{,}652\text{ km}^2$**—only **$0.033\%$ ($\frac{1}{3,024}$) of Earth's total surface area** (roughly the size of Greece or Florida).
- Historically, because $R / r_0 \approx 1.50$ and the sun previously orbited with **zero axial tilt ($\varepsilon = 0^\circ$)** in the equatorial plane, the inner hole ($v \approx \pm\pi$) suffered a $5:1$ circumference pinch and was permanently shadowed by the outer ring, freezing roughly half the planet into an uninhabitable ice zone. This is now resolved via **axial tilt ($\varepsilon = 28^\circ$)** in [`field.h`](file:///home/quirinpa/site/external/nd-world/include/nd/field.h#L47-L60) (`ND_TORUS_AXIAL_TILT_RAD`).

### 4.2 Evaluated Geometric Alternatives & Definite Decisions
During design exploration, four geometric models were evaluated against the goals of **maximizing playable habitable surface**, **preserving clean coordinate reasoning (equator, poles, climate)**, and **supporting a true 3D travelable space simulator**:

1. **Slender Ring Torus ($R \gg r_0$, e.g., $R / r_0 \ge 6$) — *Rejected as Default Planet Shape***:
   - *Reason*: While a thin hoop opens up the central hole, aesthetically it looks like an artificial *Halo* ringworld or a tire floating in space rather than a natural planet.
2. **Degenerate Torus / Double-Covered Sphere ($R \to 0$) — *Rejected for Torus Maps***:
   - *Math*: Setting $R = 0$ in the 3D torus equation $X = (R + r_0 \cos v)\cos u,\; Y = (R + r_0 \cos v)\sin u,\; Z = r_0 \sin v$ yields a sphere where $v \in [-\pi, \pi)$ double-covers the globe: $(u, v)$ and $(u + \pi, \pi - v)$ map to the same 3D point (a $\mathbb{T}^2 / \mathbb{Z}_2$ pillow orbifold).
   - *Reason*: It destroys independent doubly-periodic $(x, y)$ map wrapping and pinches an entire row of $N_X$ rooms into a single point at the North and South poles ($\cos v = 0$ at $v = \pm\pi/2$).
3. **4D Clifford Torus & Local Spherical Horizon Projection Tricks — *Rejected***:
   - *Math*: By the **Gauss–Bonnet Theorem** ($\iint_S K\,dA = 2\pi(2 - 2g)$), a sphere ($g=0$) has total Gaussian curvature $+4\pi$, whereas a torus ($g=1$) has total Gaussian curvature $0$. No rigid surface in 3D Euclidean space ($\mathbb{R}^3$) can have constant positive curvature everywhere and doubly-periodic torus topology. The **Clifford Torus** $X(u,v) = \frac{R}{\sqrt{2}}(\cos u, \sin u, \cos v, \sin v)$ achieves a flat intrinsic metric ($K=0$) only in **4D space ($\mathbb{R}^4 / S^3$)**, and its stereographic projection into $\mathbb{R}^3$ is a standard 3D torus with $R / r_0 = \sqrt{2} \approx 1.414$.
   - *Reason*: Making a torus look locally like a sphere everywhere in 3D requires a camera-relative rolling projection trick. **Projection tricks are explicitly rejected** because:
     1. They break geometric honesty—there is no real 3D body in space that matches what you see on the ground, making seamless space-to-ground flight in a space simulator impossible.
     2. Reasoning about where the equator, poles, orbital plane, and solar angles are becomes artificial and inconsistent in 4D/projected coordinates.
4. **Geometrically Honest 3D Torus with Axial Tilt ($\varepsilon > 0$) — *Implemented for Toroidal Planets***:
   - *Math & Climate Physics (Anders Sandberg model)*: A true 3D Euclidean torus ($R / r_0 \approx \sqrt{2}\text{ to }2.5$) with a non-zero **axial tilt $\varepsilon$** (tunable by the moderator or procedurally derived from the world seed, e.g., $28^\circ$ on World 0) tilts its spin axis relative to its orbital plane around the star:
     $$\mathbf{p}(u, v, h) = \mathbf{c}_{\text{body}} + \mathbf{R}_{\text{body}} \begin{pmatrix} (R + (r_0 + h)\cos v)\cos u \\ (R + (r_0 + h)\cos v)\sin u \\ (r_0 + h)\sin v \end{pmatrix}$$
   - As the planet orbits its star (seasonal angle $\tau \in [0, 2\pi)$), the solar declination $\delta$ satisfies $\sin\delta = \sin\varepsilon \sin\tau$, giving the 3D sun vector in the planet's local frame:
     $$\mathbf{s}(\theta_{\text{day}}, \tau, \varepsilon) = \begin{pmatrix} \cos\theta_{\text{day}} \cos\delta \\ \sin\theta_{\text{day}} \cos\delta \\ \sin\delta \end{pmatrix}$$
   - **Why this solves the inner hole & maximizes playable surface**:
     - Whenever $\delta \neq 0$ (all year except brief equinox crossings), sunlight shines obliquely **over the front lip of the torus directly onto the opposite inner wall (the hubward equator, $v \approx \pm\pi$)**.
     - The inner hole is no longer a permanently dark ice trap; it becomes a rich, habitable **"Archworld" climate zone** with **double seasons** (two warm sunlit summers per orbital year—one during Northern summer and one during Southern summer—separated by brief twilight winters at the equinoxes) and dramatic skies where the opposite continent arches overhead.
     - Every coordinate has unambiguous, physically honest meaning: **outer equator** at $v = 0$, **inner (hubward) equator** at $v = \pm\pi$, and **North/South crown poles** at $v = \pm\pi/2$.
   - **Zero-Projection-Trick Precision in WebGL2 (`nd_seven_gl.c`)**:
     - Both the CPU ([`torus_frame_double`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c#L3827-L3897)) and the GPU vertex shaders ([`s_vs_terrain`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c#L388-L435), [`s_vs_water`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c#L2044-L2091), [`s_vs_cloud`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c#L3347-L3380)) evaluate the exact 3D ring torus equation above, rotated by the body's $3\times 3$ orientation matrix `uBodyRot` and translated by `uPlanetCenter`.
     - To eliminate 32-bit float catastrophic cancellation at ground scale ($R = 2{,}000{,}000\text{ wu}$ vs. $1\text{ wu} = 4\text{ cm}$), vertex shaders use the exact trigonometric subtraction identity $\cos(u_0 + \Delta u) - \cos u_0 = -2\sin^2(\Delta u/2)\cos u_0 - \sin\Delta u\sin u_0$ relative to the reference origin `uRefTrig`. This is algebraically identical to the true 3D Euclidean coordinates—zero projection distortion is introduced.

### 4.3 Scaling Planetary Surface Area (Smaller than a Moon to Larger than Earth)
By combining **per-world lattice dimensions** with **tunable room physical dimensions** (`room_w`, `room_h`, `room_d`), any spatial world can be sized from a tiny asteroid to a Super-Earth without touching server memory:

| World Example | Room Size (`wu` / `m`) | Surface Lattice ($b_x \times b_y$) | Flat Surface Area | Comparison to Earth ($5.10 \times 10^8\text{ km}^2$ total / $1.49 \times 10^8\text{ km}^2$ land) |
| :--- | :--- | :--- | :--- | :--- |
| **Pocket Moon / Asteroid** | $160 \times 128\text{ wu}$ ($6.4 \times 5.1\text{ m}$) | $2^{14} \times 2^{14}$ | $8{,}796\text{ km}^2$ | **$0.0017\times$ Earth** (Cyprus / Puerto Rico) |
| **Current World 0 Baseline** | $160 \times 128\text{ wu}$ ($6.4 \times 5.1\text{ m}$) | $2^{16} \times 2^{16}$ | $140{,}737\text{ km}^2$ ($168{,}652\text{ km}^2$ torus) | **$0.00033\times$ Earth** (Greece / Florida) |
| **Earth-Scaled $(2^{16})^3$ Room Size** | $2{,}560 \times 2{,}048\text{ wu}$ ($102 \times 82\text{ m}$) | $2^{16} \times 2^{16}$ | $\mathbf{3.603 \times 10^7\text{ km}^2}$ | **$\approx 2\times$ Russia / $24\%$ of Earth's Land** (zero change to $16$-bit `coord_t`!) |
| **Earth-Landmass World** | $320 \times 256\text{ wu}$ ($12.8 \times 10.2\text{ m}$) | $2^{20} \times 2^{20}$ | $\mathbf{1.441 \times 10^8\text{ km}^2}$ | **$\approx 1.0\times$ Earth's Total Land Area** (with $2^8 = 256$ vertical $Z$ layers!) |
| **Full Earth-Surface World** | $320 \times 256\text{ wu}$ ($12.8 \times 10.2\text{ m}$) | $2^{21} \times 2^{21}$ | $\mathbf{5.765 \times 10^8\text{ km}^2}$ | **$\approx 1.13\times$ Earth's Entire Surface** (with $2^6 = 64$ vertical $Z$ layers) |
| **Super-Earth** | $640 \times 512\text{ wu}$ ($25.6 \times 20.5\text{ m}$) | $2^{21} \times 2^{21}$ | $\mathbf{2.306 \times 10^9\text{ km}^2}$ | **$\approx 4.52\times$ Earth's Entire Surface** |

---

## 5. `libcorolla`: Botanical 3D Surface & Orbital Irradiance Library

Following the repository's **botanical naming tradition** (`libxylem`, `libcorm`, `libbud`, `liblemma`, `libsepal`, `libchaff`, `libareole`, `liblamina`, `libvelum`, `libstele`), the standalone 3D parametric surface, self-occlusion, rigid-body orbital pose, and irradiance library is named **`libcorolla`** ([`external/libcorolla`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h), header `<ttypt/corolla.h>`):
- **Botanical & Mathematical Meaning**: In botany, the *corolla* is the ring/whorl of petals that forms a curved 2-manifold (a ring, cup, bell, tube, or torus) specifically evolved to orient toward and catch **sunlight (irradiance)**—and in Latin/astronomy, *corolla* literally means *"little crown / ring / halo"*.
- **Generic Scope**: To remain **as generic as possible while abstracting away a specific class of mathematical problems**, `libcorolla` knows **nothing** about "planets", "rooms", "players", or "NeverDark". It solves one self-contained mathematical domain: **parametric 2-manifolds embedded in $\mathbb{R}^3$, 3D rigid-body Keplerian poses & patch bounding boxes, analytical ray–manifold intersection / self-occlusion, and directional irradiance over spin/orbital cycles**.

### 5.1 Strict Separation Between Prefix Storage and 3D Surface Geometry
1. **Prefix & Spatial Indexing Layer (`nd-region` + `libislet`)**:
   - Knows only the 64-bit prefix tree (`st_rec`), and (when spatial) Morton coordinates and room sizes.
   - Completely agnostic to whether a region is embedded on a 3D surface or is a pure text graph.
2. **Parametric 3D Surface & Irradiance Library (`libcorolla`)**:
   - Pure C, zero UI/DB dependencies, zero hardcoded game magic constants (all parameters passed via clean descriptors, matching [`libchaff`](file:///home/quirinpa/site/external/libchaff/include/ttypt/chaff.h) and [`libareole`](file:///home/quirinpa/site/external/libareole/include/ttypt/areole.h)), dual-compiling natively on the server and to WASM on the client (`<ttypt/iso.h>`).
   - Supports generic parametric surface families:
     - **`COROLLA_TORUS`** ($R, r_0$): $X = (R + r_0 \cos v)\cos u,\; Y = (R + r_0 \cos v)\sin u,\; Z = r_0 \sin v$.
     - **`COROLLA_SPHERE`** ($R_x, R_y, R_z$): standard spherical/ellipsoidal embedding.
     - **`COROLLA_CYLINDER`** ($R, H$): cylindrical habitats or ribbons.
     - **`COROLLA_PLANE`** ($W, H$): flat Euclidean sheet.

### 5.2 Core Generic Primitives of `libcorolla`
1. **Local Differential Frame ([`corolla_eval_frame`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h#L187-L189))**:
   - Given surface descriptor + normalized surface parameters $(u, v) \in [0, 1)^2$ + normal offset $h$:
   - Computes 3D position $\mathbf{p} \in \mathbb{R}^3$, unit normal $\mathbf{n}$, unit tangent vectors $(\mathbf{t}_u, \mathbf{t}_v)$, and local metric scale factors $(ds_u, ds_v)$.
   - *What this abstracts away*: Both ground cameras (computing local horizon curvature and sky direction) and spaceflight physics (converting between `(u, v, h)` and 3D Cartesian coordinates) call the exact same function.
2. **Exact Ray–Manifold Intersection & Self-Occlusion ([`corolla_ray_hit`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h#L204-L208) / [`corolla_occluded`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h#L217-L219))**:
   - Given a 3D ray $\mathbf{r}(t) = \mathbf{o} + t\mathbf{d}$, solves the exact analytical intersection against the manifold (quadratic for sphere/cylinder, quartic root solver for torus) and returns hit distance $t > 0$ and surface coordinates $(u_{\text{hit}}, v_{\text{hit}})$.
   - *What this abstracts away*:
     1. **Self-shadowing**: Determines instantly if a point $(u, v)$ on the inner hub of a torus is shadowed by the torus's own outer lip along light vector $\mathbf{s}$.
     2. **Sky-arch & spacecraft raycasting**: Determines where a player's line of sight hits the opposite side of their own toroidal world arching across the sky, or where a spacecraft ray intersects a neighbouring body.
3. **3D Rigid-Body Spin, Axial Tilt, Keplerian Poses & Patch AABBs ([`corolla_orbit_pos`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h#L262-L264), [`corolla_eval_body_pose`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h#L278-L281), [`corolla_pose_relative`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h#L308-L311), [`corolla_patch_aabb`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h#L324-L331))**:
   - Solves Kepler's equation ($M = E - e\sin E$) for 3D orbital translation and evaluates orthonormal $3\times 3$ body orientation matrices $\mathbf{R} = \mathbf{R}_z(\phi_{\text{az}})\mathbf{R}_x(-\varepsilon)\mathbf{R}_z(\theta_{\text{day}})$ (`corolla_pose_t`).
   - Transforms positions and directional vectors between inertial, parent, and observer body frames (`corolla_pose_vec_to_inertial`, `corolla_pose_vec_to_body`, `corolla_pose_point_to_inertial`, `corolla_pose_relative`), allowing the observer's planet to remain at $\{0, 0, 0\}$ with identity rotation while companion worlds orbit and spin in the observer's frame.
   - Computes tight conservative 3D axis-aligned bounding boxes (`corolla_patch_aabb`) for any curved manifold patch $[u_0, u_0 + du] \times [v_0, v_0 + dv] \times [h_{\min}, h_{\max}]$ under arbitrary rigid-body pose and camera reference origin by evaluating all 8 patch corners plus analytical interior trigonometric extrema ($\phi, \theta = k\pi/2$).
4. **1D Annual Insolation LUT ([`corolla_bake_annual_insolation_lut`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h#L417-L421), [`corolla_sample_annual_insolation_lut`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h#L424-L426))**:
   - Because a rotating world averages longitudinal sunlight uniformly around $u \in [0, 1)$, mean annual self-shadowed insolation $\bar{I}_{\text{year}}(v)$ is strictly a 1D function of poloidal/latitudinal coordinate $v \in [0, 1)$. Baking a 64-entry LUT once per world (`corolla_climate_model_init`) reduces per-room solar climate evaluation to an $O(1)$ linear interpolation with zero runtime quartic raycasts.
5. **Coupled `libcorolla` + `libchaff` Planetary Wind, 4D Cloud Advection & Whittaker Climate ([`corolla_eval_wind`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h#L439-L445), [`corolla_eval_climate`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h#L454-L462), [`corolla_whittaker_classify`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h#L432-L435))**:
   - **Coriolis 3-Cell Circulation & Curl-Noise Eddies**: Evaluates tangent wind $(w_u, w_v)$ and vertical uplift $w_\perp$ from the surface normal's spin-axis component $n_z = \mathbf{n} \cdot \hat{\mathbf{k}}$ (Trade Winds, Westerlies, Polar Easterlies via $-\text{spin} \cdot \cos(3\arcsin n_z)$), subsolar thermal draft toward `sun_dir`, divergence-free curl-noise eddies via `chaff_sample_flow_2d()`, and windward orographic slope uplift ($\mathbf{w} \cdot \nabla H$).
   - **Semi-Lagrangian 4D Cloud Advection**: Back-traces $(u, v) - \mathbf{w}\,\Delta\tau$ into `chaff_sample_grad_4d()` to evaluate advected `cloud_cover` and active `precipitation`.
   - **Surface-Agnostic Whittaker Biome Table**: Maps permanent `(mean_temp, mean_moisture)` into the 19-biome taxonomy (`COROLLA_BIOME_WATER`..`COROLLA_BIOME_VOLCANIC`) with $C^1$ quadratic border blending (`primary_biome`, `secondary_biome`, `biome_blend`), shared identically by [`nd/field.h`](file:///home/quirinpa/site/external/nd-world/include/nd/field.h) and [`mods/nd/ux/nd_seven_gl.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c).

### 5.3 Unified 3D Multi-Body Rendering & Spatial Culling (`libareole` + `nd-world` + `mods/nd/ux`)
1. **Toroidal Wrapping & 3D Manifold Frustum Culling in [`libareole`](file:///home/quirinpa/site/external/libareole/include/ttypt/areole.h)**:
   - `areole_config_t` supports periodic domain wrapping (`period_x`, `period_y`) so distance metrics and quadtree subdivision wrap seamlessly across $(u, v) \in [0, 1)^2$ boundaries without seam artifacts.
   - `areole_view_t` accepts a 3D bounding-box callback (`bounds_3d_fn`) paired with `areole_frustum_test_aabb`, enabling true 3D frustum culling of curved planetary and lunar quadtree patches in [`seven_torus_bounds_3d_cb`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c#L377-L418).
2. **Unified Celestial Ephemeris & Astronomical Ocean Tides in [`nd/field.h`](file:///home/quirinpa/site/external/nd-world/include/nd/field.h)**:
   - [`nd_field_world_body`](file:///home/quirinpa/site/external/nd-world/include/nd/field.h#L253-L296) defines the canonical `corolla_body_t` orbital and spin descriptors for World 0 (`ND_WORLD_PRIMARY`), World 1 (`ND_WORLD_MOON`, inclined Keplerian orbit around World 0), and World 2 (`ND_WORLD_OUTER`).
   - [`nd_field_eval_celestial_ephemeris`](file:///home/quirinpa/site/external/nd-world/include/nd/field.h#L393-L441) computes both the instantaneous 3D rigid-body poses for [`nd_seven_eval_planet_at_time`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c#L334-L370) and the body-frame `sun_dir` / `moon_dir` vectors for [`nd_seven_gl_eval_celestial_dirs`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c#L6935-L6952), guaranteeing that semidiurnal ocean tides pull directly toward the rendered 3D Moon in the sky across all camera altitudes.
3. **Encapsulation & CPU/GLSL Parity (`libchaff` + `nd_seven_shader.h`)**:
   - [`libchaff`](file:///home/quirinpa/site/external/libchaff/include/ttypt/chaff_glsl.h) exposes parameterized GLSL noise/flow blocks (`CHAFF_GLSL_NOISE_CORE`, `CHAFF_GLSL_SYNOPTIC_CORE`, `CHAFF_GLSL_WIND_CORE`) with zero hardcoded game constants; NeverDark-specific atmospheric tuning lives cleanly in [`nd_seven_shader.h`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_shader.h).

---

## 6. Per-Prefix Programmable Policies: Recursive `wall`, Radial Chat & Reversible Deltas

Because NeverDark is a programmable engine where moderators control prefixes at any depth $k$ of the 64-bit tree in `nd-region`, communication and persistence rules are **configurable policies inherited down the prefix tree**:

1. **Recursive Moderator `wall` ([`do_wall`](file:///home/quirinpa/site/external/nd-speech/src/speech.c#L209-L294))**:
   - A moderator owning prefix $(P, k)$ (or targeting a sub-prefix they rule) can broadcast an announcement to **all subjects currently inside that prefix subtree**—recursively spanning every delegated sub-region beneath $(P, k)$ via `st_region_covers(P, k, player_cell, 64)`.
2. **Separate Radial Chat Function vs. Room-Local `nd_dowritef()`**:
   - **`nd_dowritef()` remains strictly room-local**: It continues to call `nd_rwrite(player.location, ...)` for immediate room events, indoor rooms, and non-spatial MUCK prefixes without change.
   - **Dedicated Radial Propagation Function ([`nd_radial_rwrite()`](file:///home/quirinpa/site/external/nd-speech/src/speech.c#L55-L122) & [`st_region_get_chat_radius()`](file:///home/quirinpa/site/external/nd-region/src/libnd-region.c#L733-L759))**:
     - Speech/chat calls [`nd_radial_rwrite()`](file:///home/quirinpa/site/external/nd-speech/src/speech.c#L55-L122), which always delivers to the local room via `nd_rwrite()`, and—if the governing prefix has `chat_radius > 0` (resolved via [`st_region_get_chat_radius()`](file:///home/quirinpa/site/external/nd-region/src/libnd-region.c#L733-L759)) and the room is outdoors—propagates across nearby rooms within $R_{\text{chat}}$ with directional cues (e.g., `"[from the north] ..."`), decoupling **room size** from outdoor speech range.
3. **Timestamped `ROO.last_mod` Lazy Eviction & `st_rec` Room Quotas**:
   - `ROO` stores `uint32_t last_mod` inside existing `OBJ.data` space. When an unobserved `RF_TEMP | RF_MODIFIED` room is checked by [`check_deterministic_recovery()`](file:///home/quirinpa/site/external/nd-world/src/spacetime.c#L599-L671) and `now - rhere->last_mod >= tau_regrow` (with no foreign items), the room record is deleted from `libcorm` (`0` bytes on disk), so its next visit naturally synthesizes fresh trees, sticks, and terrain from the deterministic seed.
   - Permanent room claims ([`carve()`](file:///home/quirinpa/site/external/nd-world/src/spacetime.c#L725-L785) / [`st_region_claim_room()`](file:///home/quirinpa/site/external/nd-region/src/libnd-region.c#L761-L802)) increment `used_rooms` against `quota_rooms` in the governing `nd-region` `st_rec`, and [`uncarve()`](file:///home/quirinpa/site/external/nd-world/src/spacetime.c#L788-L824) / [`st_region_unclaim_room()`](file:///home/quirinpa/site/external/nd-region/src/libnd-region.c#L804-L829) decrements it.

---

## 7. Runtime-Parameterized Room Geometry, Isomorphic Canopy Occupation & Universal Cast Shadows

To bridge the discrete MUCK room/slot persistence model (`plant_tile_t`, `HD_OBJ` slots `side = 0..2`) with a physically scaled, continuous 3D world where mature trees tower over the $18\text{ px}$ lamb (`NDS_LAMB`), four architectural invariants govern room geometry, flora ecology, normal-mapped lighting, and shadow projection:

### 7.1 Runtime-Parameterized Room Dimensions (No Compile-Time Room Size Constants)
1. **Eliminating Hardcoded `160 × 128` Macro Assumptions**:
   - Room physical dimensions (`room_w`, `room_h`, `room_d`) must not be hardcoded compile-time constants (`ND_FIELD_ROOM_W = 160`, `NDW_TILE_W = 160`) or scattered magic literals (`80`, `64`, `44`, `72`, `98`, `116`, `55`, `105` in [`noise.c`](file:///home/quirinpa/site/external/nd-world/src/noise.c#L61-L62) and [`seven-world.h`](file:///home/quirinpa/site/external/nd-seven/include/nd/seven-world.h#L541-L602)).
   - Instead, `(room_w, room_h, room_d)` belong to the runtime world/prefix geometry descriptor (governed by `nd-region` / `nd_planet_def_t`), defaulting to `(160, 128)` for baseline worlds while allowing any world or sub-moderator prefix to scale rooms dynamically (e.g., $320 \times 256$, $640 \times 512$, or $2{,}560 \times 2{,}048\text{ wu}$ as in §4.3).
2. **Proportional Sub-Room Geometry**:
   - Every intra-room spatial quantity in [`seven-world.h`](file:///home/quirinpa/site/external/nd-seven/include/nd/seven-world.h) is derived proportionally at runtime from `(room_w, room_h)`:
     - Horizontal slot domains (`s = 0, 1, 2`), flank clear zones around the room center `(room_w / 2, room_h / 2)`, wall thicknesses, and gate widths.
     - Organic border corner-rounding radii (`corner_rx = room_w / 2`, `corner_ry = room_h / 2`) and warp-grid node spacing (`warp_step = room_w / 80`), ensuring `ndw_warp_grid_t` maintains a constant $81 \times 65$ node memory footprint regardless of physical room size.

### 7.2 Strict Server–Client Isomorphism for World Contents
1. **Single Shared Rule Across Server and Client**:
   - Just as biomes, terrain elevation, water levels, and solar daylight are evaluated isomorphically via [`<nd/field.h>`](file:///home/quirinpa/site/external/nd-world/include/nd/field.h), the exact same deterministic rules that spawn trees, plants, and minerals must execute identically on the server ([`libnd-plant.c`](file:///home/quirinpa/site/external/nd-plant/src/libnd-plant.c), [`libnd-stone.c`](file:///home/quirinpa/site/external/nd-stone/src/libnd-stone.c)) and the client ([`seven-world.h`](file:///home/quirinpa/site/external/nd-seven/include/nd/seven-world.h)).
2. **1 Server Entity = 1 Client Object (`count_base = 1, count_rand = 0`)**:
   - On the server, [`_plants_add()`](file:///home/quirinpa/site/external/nd-plant/src/libnd-plant.c#L191-L219) materializes at most **one** `HD_OBJ` plant entity per occupied room slot (`side = 0, 1, 2`).
   - The client must never fabricate multiple clustered mature tree billboards (`count_base = 2, count_rand = 1` at $19\text{–}36\text{ wu}$ separation) for a single server slot. Every occupied slot corresponds 1-to-1 between the interactive `HD_OBJ` entity in `libcorm` and the rendered 3D billboard on stage.

### 7.3 Procedural Canopy Occupation & Resource Competition (Room-Size Independent)
1. **No Hardcoded Per-Biome Tree Caps (`max_trees`)**:
   - Vegetation density is a continuous **physical world-space property** (governed by sunlight and water per square meter), never an arbitrary per-room integer cap and never dependent on room dimensions `(room_w, room_h)`.
2. **Crown Area & Moisture Catchment Model in [`<nd/plant.h>`](file:///home/quirinpa/site/external/nd-plant/include/nd/plant.h)**:
   - Each botanical species in [`s_nd_plant_specs`](file:///home/quirinpa/site/external/nd-plant/include/nd/plant.h#L72-L89) defines its physical mature crown diameter $W_{\text{crown}}$ (calibrated to the lamb scale in [`PROMPT.md`](file:///home/quirinpa/site/PROMPT.md), e.g., $270\text{ wu}$ for *Acacia senegal*) and its canopy stratum (**overstory tree** vs. **understory shrub** vs. **ground-layer herb**).
   - A tree requires both **canopy sunlight area** ($\pi R_{\text{crown}}^2$) and **root moisture catchment area** ($\pi R_{\text{root}}^2$):
     $$R_{\text{eff}}(rn, tmp) = R_{\text{crown}} \cdot \max\!\left(1.0,\; \sqrt{\frac{rn_{\text{optimal}}}{rn_{\text{local}}}}\right)$$
   - **Arid / Semi-Arid Biomes (Savannah, Dry Woodland, Steppe, Desert)**: Low rainfall `rn` expands $R_{\text{eff}} \gg R_{\text{crown}}$, naturally spacing mature trees across multiple rooms so they do not compete for scarce groundwater, leaving open vistas between solitary crowns.
   - **Humid Forests (Rainforest, Seasonal, Boreal, Coniferous)**: High rainfall `rn` means $R_{\text{eff}} \approx R_{\text{crown}}$; sunlight competition (crown shyness) governs spacing. Once an overstory tree claims the upper canopy light budget at a coordinate, adjacent sub-canopy space can only be occupied by shade-tolerant understory flora (ferns, mosses, herbs with `col_mask == 0`) rather than piling multiple mature tree trunks onto the same spot.
   - **Cross-Room Canopy Continuity**: Tree crowns and root zones are evaluated in continuous world coordinates $(wx, wy)$ and freely overhang across room boundaries without clipping.

### 7.4 Botanical Normal Mapping & Universal 3D Silhouette Shadows
1. **Lamb-Calibrated Unlit Diffuse + Tangent-Space Normal Maps (`uAtlasNormTex`)**:
   - Botanical sprites generated via [`PROMPT.md`](file:///home/quirinpa/site/PROMPT.md) combine an unlit diffuse pixel-art texture (hierarchical macro dome + crisp leaf/bark clusters on a `#FF00FF` chroma key, scaled strictly to the $18\text{ px}$ lamb) with a paired OpenGL/WebGL tangent-space normal map (`<var>_n.png`, $+X$ right, $+Y$ up, $+Z$ out).
   - The billboard fragment shader ([`s_fs_billboard`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c#L3142-L3210)) unpacks `uAtlasNormTex` (flipping $N_x$ when a sprite is horizontally mirrored) and evaluates directional diffuse, foliage transmittance, rim lighting, and local point-light irradiance (`uLambLight`) against the live 3D solar vector (`uSunDir`).
2. **Universal Sun-Projected Silhouette Shadows for All Stage Objects**:
   - Every 3D billboard on the stage—**trees, shrubs, stones, rocks, boulders, mobs/entities, and the lamb**—casts a real 3D ground shadow onto the continuous terrain mesh ([`eval_billboard_shadow_quad_with_origin`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c#L4212-L4275) & [`s_fs_billboard`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c#L3034-L3051)).
   - Instead of drawing a static circular blob underneath each object, the shadow quad projects the billboard's actual atlas UV silhouette (`uAtlasTex` within `vSpriteBounds`) onto the local surface tangent plane $(\mathbf{E}, \mathbf{S})$ along the live 3D sun vector `uSunDir` (anchored at the foot contact point, elongated by solar zenith angle and object height `b->height`, coupled with wind sway and a soft contact umbra at the base).

---

## 8. Embedding-Driven Semantic Game AI & Hybrid Utility Controllers (`libsepal` + `libcorm`)

An embedding model does not generate text or decisions directly; it maps text into a vector space where **similarity equals relevance**. To drive fast, non-generative NPC and mob decisions from text-formatted game state, decision-making is framed as **vector retrieval / classification**—matching the current state vector against candidate action vectors or exemplar state-action pairs rather than running slow autoregressive LLM decoding.

### 8.1 Three Architectures for Turning Embeddings into Game Decisions

1. **Zero-Shot Action Scoring (State $\to$ Action Prototypes)**:
   - Pre-compute embeddings for a fixed set of discrete actions or behaviors (written as descriptive situations/intents), embed the current state at runtime, and pick the action with the highest **cosine similarity** (or feed the similarities into a softmax / utility curve):
     - **Pre-computed action anchors**:
       - `ACT_FLEE`: *"Critically wounded, low health, outnumbered, needs to retreat to safety"*
       - `ACT_FLANK`: *"Enemy is pinned down and distracted, high stamina, opportunity to attack from behind"*
       - `ACT_HEAL`: *"Safe behind cover, ally or self is injured, healing potion available"*
     - **Runtime**: Format qualitative state $\to$ embed $\to$ dot product against anchors via [`libsepal`](file:///home/quirinpa/site/external/libsepal/include/ttypt/sepal.h) $\to$ selects `ACT_FLEE`.
2. **Case-Based Reasoning ($k$-NN over State $\to$ Action Memory)**:
   - Rather than matching state-to-action directly (which can suffer from the modality gap between *"what is happening"* and *"what to do"*), store a library of **(Exemplar State Description $\to$ Chosen Action / Utility)** pairs in [`libcorm`](file:///home/quirinpa/site/external/libcorm/include/ttypt/corm.h) + [`libsepal`](file:///home/quirinpa/site/external/libsepal/include/ttypt/sepal.h):
     - Embed $500\text{–}5{,}000$ designer-authored or playtest-recorded situations.
     - At runtime, embed the current game state, find the top-$k$ nearest historical states via dot product, and take a distance-weighted vote of their actions.
     - **Episodic Adaptation**: NPCs can dynamically learn or adapt by inserting new `(state_vec, action, outcome_reward)` tuples into their episodic memory at runtime.
3. **Tiny Policy Head on Top of Frozen Embeddings**:
   - Use the text embedding (`384d` or `768d` float vector) as the feature extractor and pass it through a tiny 1- or 2-layer MLP (a couple of matrix multiplies in pure C) trained via imitation learning or RL to output action logits. This resolves arithmetic/threshold blind spots while retaining the open-ended semantic flexibility of text.

### 8.2 Strengths vs. Failure Modes of Text Embeddings in Game AI

| Strength | Limitation |
| :--- | :--- |
| **Fuzzy / Open-Ended Context**: Ideal for social state, faction attitudes, dialogue tone, rumors, weather/biome vibe, or emergent tags (`"player betrayed the guild and is carrying a cursed relic"`). | **Exact Numbers & Thresholds**: Text embeddings are poor at raw numbers (`"HP: 49"` vs. `"HP: 51"` or `"10m away"` vs. `"100m away"` look nearly identical in embedding space). |
| **Fixed Millisecond Latency vs. LLMs**: A single forward pass of a small embedding model (`all-MiniLM`, `bge-small`, or local ONNX/GGUF) takes **$1\text{–}5\text{ ms}$**, whereas autoregressive LLM decoding takes hundreds of milliseconds. | **Combinatorial / Spatial Reasoning**: Pathfinding, line-of-sight geometry, turn-ahead tactics, and strict Boolean preconditions (`has_key == true`) should never be delegated to text embeddings. |
| **Zero-Schema Extensibility**: Adding a new status effect, biome mood, or item does not require rewiring a fixed-size observation tensor—its text description automatically lands near semantically similar concepts. | **Negation Blindness**: `"Enemy is NOT armed"` and `"Enemy is armed"` often have high cosine similarity in smaller embedding models; represent negations via distinct positive descriptors (`"unarmed"` vs. `"heavily armed"`). |

### 8.3 Best Practice: Qualitative Pre-Bucketing & the Hybrid Utility AI Pattern

To use embeddings reliably in NeverDark without falling into numeric or precondition traps:

1. **Pre-Bucket Numbers into Semantic Words Before Embedding**:
   - Never pass raw numbers like `"hp: 14/100, dist: 2.1, enemies: 4"`. Translate numeric state into qualitative, polarity-distinct buckets first:
     `"Health: critical. Range: melee. Threat: outnumbered. Environment: dark thunderstorm."`
2. **Use Embeddings as One Term in a Hybrid Utility AI Scorer**:
   - Combine hard game rules (masks/multipliers) and crisp tactical math with the embedding similarity score:
     $$\text{Score}(a) = \text{HardMask}(s, a) \times \left( w_{\text{sim}} \cdot \cos(\vec{e}_s, \vec{e}_a) + w_{\text{utility}} \cdot U(s, a) \right)$$
   - `HardMask(s, a)` strictly zeroes out impossible actions (on cooldown, out of range, missing item/mana, blocked line of sight).
   - $\cos(\vec{e}_s, \vec{e}_a)$ provides the semantic "vibe/context" score (or $k$-NN episodic memory vote from [`libsepal`](file:///home/quirinpa/site/external/libsepal/include/ttypt/sepal.h) + [`libcorm`](file:///home/quirinpa/site/external/libcorm/include/ttypt/corm.h)).
   - $U(s, a)$ handles exact tactical math (expected damage, distance, stamina cost).

### 8.4 Client-Offloaded Browser Embedding Search & Exclusive Single-Client Mob Leases

To preserve the **Lean OpenBSD Server** invariant (§1), the server **never** runs embedding inference or vector similarity search. All embedding generation and [`libsepal`](file:///home/quirinpa/site/external/libsepal/include/ttypt/sepal.h) vector lookup run inside the **client browser (WASM / WebGPU)**, while the server acts solely as a lightweight lease coordinator and authoritative rule validator:

1. **Zero Server-Side Embedding Compute**:
   - The client browser formats the local room/mob state into qualitative text, computes the state embedding locally, runs the cosine similarity / $k$-NN lookup against the mob's action prototypes or case memory in client WASM, and submits only a compact **chosen action intent** (e.g., `(mob_ref, lease_epoch, action_id, target_ref)`) to the server.
2. **Exclusive Single-Client Control Leases (No Split-Brain Mob Control)**:
   - A mob controlled by one client **cannot** be simultaneously controlled by another client in the same or adjacent room.
   - Each active mob carries an exclusive server-side lease `(controller_player_ref, lease_epoch, lease_expires_at)`:
     - **Acquisition**: When an unobserved mob enters the active observation horizon of one or more players, the server assigns the exclusive control lease to **at most one** observing client (e.g., the first observer in the room, or lowest-RTT / round-robin across nearby clients to spread browser load).
     - **Fencing via `lease_epoch`**: Every AI action packet sent by a client must carry the current `lease_epoch` and originate from `controller_player_ref`. Any action packet from a non-holder or with a stale epoch is silently dropped by the server.
     - **Failover & Handoff**: If the controlling client leaves the observation radius, disconnects, or lets its lease heartbeat expire (`now > lease_expires_at`), the server increments `lease_epoch` and hands the lease to the next observing client in the area—or, if no players remain nearby, evicts the mob back to zero-cost deterministic dormancy (§2.2).
3. **Server-Side Authoritative Validation (`HardMask` Enforcement)**:
   - Even though the leased client chooses *which* action the mob wants to take, the server still validates the resulting command against authoritative game rules (cooldowns, range, line-of-sight, movement speed, and valid action set) so a modified client cannot make a leased mob cheat or perform illegal state transitions.

---

## 9. Summary of Definite Decisions, Implementation Status & Remaining Open Explorations

### Definite Architectural Decisions (All Implemented & Verified)

| # | Architectural Decision | Status | Primary Implementation |
| :--- | :--- | :--- | :--- |
| **1** | **Opaque 64-Bit Keys at the Base (Spatial Optional)** | **Implemented** | [`region-types.h`](file:///home/quirinpa/site/external/nd-region/include/nd/region-types.h#L30-L46), [`islet.h`](file:///home/quirinpa/site/external/libislet/include/ttypt/islet.h) |
| **2** | **Recursive Moderator `wall` & Prefix Governance** | **Implemented** | [`do_wall`](file:///home/quirinpa/site/external/nd-speech/src/speech.c#L209-L294), [`libnd-region.c`](file:///home/quirinpa/site/external/nd-region/src/libnd-region.c) |
| **3** | **No Projection Tricks (True 3D Rigid Bodies & Axial Tilt)** | **Implemented** | [`field.h`](file:///home/quirinpa/site/external/nd-world/include/nd/field.h#L253-L441), [`nd_seven.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven.c#L334-L418), [`nd_seven_gl.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c) |
| **4** | **Isolated Botanical 3D Geometry & Orbital Library (`libcorolla`)** | **Implemented** | [`corolla.h`](file:///home/quirinpa/site/external/libcorolla/include/ttypt/corolla.h), [`corolla.c`](file:///home/quirinpa/site/external/libcorolla/src/corolla.c), [`test_corolla.c`](file:///home/quirinpa/site/external/libcorolla/tests/test_corolla.c) |
| **5** | **Reversible Modifications with Lazy `ROO.last_mod` Cooldown** | **Implemented** | [`check_deterministic_recovery`](file:///home/quirinpa/site/external/nd-world/src/spacetime.c#L599-L671), [`eng_room_clean`](file:///home/quirinpa/site/external/nd-world/src/spacetime.c#L674-L716) |
| **6** | **Separate Radial Outdoor Chat (`nd_radial_rwrite`)** | **Implemented** | [`nd_radial_rwrite`](file:///home/quirinpa/site/external/nd-speech/src/speech.c#L55-L122), [`st_region_get_chat_radius`](file:///home/quirinpa/site/external/nd-region/src/libnd-region.c#L733-L759) |
| **7** | **Lean OpenBSD Resource Model & Prefix Room Quotas** | **Implemented** | [`st_region_claim_room`](file:///home/quirinpa/site/external/nd-region/src/libnd-region.c#L761-L802), [`st_region_unclaim_room`](file:///home/quirinpa/site/external/nd-region/src/libnd-region.c#L804-L829) |
| **8** | **Abstraction-First TDD (`libchaff`, `libareole`, `libcorolla`)** | **Implemented** | [`test_corolla.c`](file:///home/quirinpa/site/external/libcorolla/tests/test_corolla.c), [`test_areole.c`](file:///home/quirinpa/site/external/libareole/tests/test_areole.c), [`test_torus_planetary_renderer.c`](file:///home/quirinpa/site/tests/unit/test_torus_planetary_renderer.c) |
| **9** | **Botanical Tangent-Space Normal Maps & Lamb-Relative Scale (`uAtlasNormTex`)** | **Implemented** | [`PROMPT.md`](file:///home/quirinpa/site/PROMPT.md), [`nd_seven_atlas_draw.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_atlas_draw.c), [`nd_seven_gl.c`](file:///home/quirinpa/site/mods/nd/ux/nd_seven_gl.c) |

### Remaining Optional Enhancements & Per-Prefix Tunables
1. **Multi-Shape GPU Shader Dispatch (`COROLLA_SPHERE` / `COROLLA_CYLINDER` / `COROLLA_PLANE` in `nd_seven_gl.c`)**:
   - `libcorolla` and `seven_torus_bounds_3d_cb` already support all four manifold families (`COROLLA_TORUS`, `COROLLA_SPHERE`, `COROLLA_CYLINDER`, `COROLLA_PLANE`). Exposing `surf.kind` as a uniform in `nd_seven_gl.c` will allow spherical moons or O'Neill cylinder habitats to render alongside toroidal worlds.
2. **Continuous Sub-Room Terrain Digging / Height Delta Persistence (§2.2.2)**:
   - Room carving (`carve`/`uncarve`), room quotas (`quota_rooms`/`used_rooms`), and flora/mineral regrowth with `last_mod` lazy eviction are live. Continuous sub-room terrain height deltas $\Delta H(x, y, z)$ with erosion cooldown $\tau_{\text{erosion}}$ remain an optional gameplay expansion.
3. **Runtime-Parameterized Per-Prefix Room Dimensions & Asymmetric `libislet` Bit Schedules (§3.4, §4.3 & §7.1)**:
   - Replacing compile-time `160 × 128` room-size macros across `nd-world` and `nd-seven` with runtime-parameterized `(room_w, room_h, room_d)` descriptors and extending `libislet` / `nd-region` with asymmetric `(bits_x, bits_y, bits_z)` bit schedules (`bits_x + bits_y + bits_z <= 48`, e.g., `20, 20, 8` for an Earth-landmass-sized world).
4. **Isomorphic Canopy-Occupation Flora Placement & Universal Silhouette Cast Shadows (§7.2–§7.4)**:
   - Unifying 1:1 server-to-client slot counts (`count_base = 1, count_rand = 0`), replacing hardcoded `max_trees` with continuous crown-area and moisture-catchment competition in [`<nd/plant.h>`](file:///home/quirinpa/site/external/nd-plant/include/nd/plant.h), and projecting atlas-silhouette ground shadows along `uSunDir` for all stage billboards (trees, rocks, boulders, mobs, and the lamb).
5. **Interactive Interplanetary Flight / Transit Camera**:
   - Adding an interactive spaceflight / orbital transit camera mode to pilot seamlessly between World 0 and companion moons (World 1 / World 2) in real time.
6. **Client-Offloaded Embedding Game AI & Exclusive Mob Control Leases (§8)**:
   - Running qualitative state embedding and [`libsepal`](file:///home/quirinpa/site/external/libsepal/include/ttypt/sepal.h) similarity search strictly inside the client browser (WASM) so the OpenBSD server does zero vector compute, coordinated by exclusive single-client mob leases `(controller_player_ref, lease_epoch, lease_expires_at)` with automatic failover and server-side rule validation.

---

## 10. Mathematical & Geometric References
- **Clifford Torus & Stereographic Projection ($\mathbb{R}^4 / S^3 \to \mathbb{R}^3$)**:
  - [Wikipedia — Clifford Torus](https://en.wikipedia.org/wiki/Clifford_torus)
  - [Virtual Math Museum — Clifford Torus in $S^3$](https://virtualmathmuseum.org/Surface/clifford_torus/clifford_torus.html)
  - [Wolfram MathWorld — Clifford Torus](https://mathworld.wolfram.com/CliffordTorus.html)
- **Toroidal Planet Physics, Gravity & Axial-Tilt Insolation**:
  - [Anders Sandberg — *What would the Earth be like if it was the shape of a donut?*](https://gizmodo.com/what-would-the-earth-be-like-if-it-was-the-shape-of-a-d-1515700296)
- **Doubly-Periodic Conformal Sphere Mappings**:
  - [Wikipedia — Peirce Quincuncial Projection](https://en.wikipedia.org/wiki/Peirce_quincuncial_projection)
- **Codebase Metrology & Planetary Math Baseline**:
  - [`WORLD_MATH.md`](file:///home/quirinpa/site/WORLD_MATH.md)
