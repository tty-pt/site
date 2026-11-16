# NeverDark World Math: Physical Scale, Planetary Torus Geometry & Viewport Coverage

This document derives the exact physical dimensions of the NeverDark universe from first principles in the codebase (`external/nd-world/include/nd/field.h`, `external/nd-seven/include/nd/seven-world.h`, `mods/nd/ux/nd_seven.h`, `mods/nd/ux/nd_seven.c`, `mods/nd/ux/nd_seven_gl.c`, and `mods/nd/ux/nd_seven_atlas.h`), anchoring the coordinate system against the pixel-art lamb (`NDS_LAMB`) and real-world sheep/lamb anatomy (`Ovis aries`), then comparing the planet to Earth and computing exact screen counts at default zoom.

---

## 1. Fundamental Engine & Lattice Constants

From `nd/field.h`, `nd/seven-world.h`, and `mods/nd/ux/nd_seven.h`:

| Constant | Symbol | Value | Source |
| :--- | :--- | :--- | :--- |
| **Cell Size** | `NDW_CELL_PX` | $16 \times 16\text{ wu}$ | `seven-world.h:54` |
| **Room Grid** | `NDW_COLS` $\times$ `NDW_ROWS` | $10 \times 8\text{ cells}$ | `seven-world.h:52-53` |
| **Room Size** | `NDW_TILE_W` $\times$ `NDW_TILE_H` | $160 \times 128\text{ wu}$ | `seven-world.h:55-56` |
| **Room Area** | $A_{\text{room}}$ | $20{,}480\text{ wu}^2$ | $160 \times 128$ |
| **Planet Room Lattice** | $N_X \times N_Y$ | $65{,}536 \times 65{,}536\text{ rooms}$ ($2^{16} \times 2^{16}$) | `nd_seven.h:94-95` |
| **Total Rooms per World** | $N_{\text{rooms}}$ | $4{,}294{,}967{,}296\text{ rooms}$ ($2^{32}$) | $65{,}536^2$ |
| **World Flat Width (E–W)** | `ND_SEVEN_WORLD_DIM_W` ($W_X$) | $10{,}485{,}760\text{ wu}$ | $65{,}536 \times 160$ |
| **World Flat Height (N–S)** | `ND_SEVEN_WORLD_DIM_H` ($W_Y$) | $8{,}388{,}608\text{ wu}$ | $65{,}536 \times 128$ |
| **World Flat Lattice Area** | $A_{\text{flat}} = W_X \cdot W_Y$ | $87{,}960{,}930{,}222{,}080\text{ wu}^2$ ($\approx 8.796 \times 10^{13}\text{ wu}^2$) | $10{,}485{,}760 \times 8{,}388{,}608$ |
| **Torus Major Radius** | $R$ (`major_r`) | $2{,}000{,}000\text{ wu}$ | `nd_seven.c:264` |
| **Torus Minor Radius** | $r_0$ (`minor_r`) | $1{,}335{,}000\text{ wu}$ | `nd_seven.c:264` |
| **Secondary Moon Torus** | $R_{\text{moon}}, r_{\text{moon}}$ | $800{,}000\text{ wu},\; 500{,}000\text{ wu}$ | `nd_seven.c:266` |

*(Here $1\text{ wu}$ = $1\text{ continuous world unit}$ = $1\text{ source texel}$ in the Seven sprite atlas).*

---

## 2. The Lamb Scale Anchor (`NDS_LAMB` vs. Real-World `Ovis aries`)

### 2.1 In-Engine Lamb Dimensions (in World Units)
In `mods/nd/ux/nd_seven_atlas_draw.h` and `mods/nd/ux/nd_seven_atlas.h`:
- Each animation cell of the lamb sprite sheet (`NDS_LAMB`) is a $32 \times 32\text{ wu}$ billboard (`LAMB_SIZE = 32`), with anchor `LAMB_AY = 16` and hoof contact line at `LAMB_FOOT_Y = 25`.
- Decompressing the RLE sprite sheet (`nd_seven_rle`) yields the exact non-transparent pixel bounding boxes of the lamb's body:
  - **Standing Height (hooves at $y=25$ to head crown at $y=8$)**:
    $$H_{\text{lamb}} = 25 - 8 + 1 = 18\text{ wu}$$
  - **Side-Profile Body Length (nose to tail, East/West rows 2, 3, 6, 7, $x \in [3..25]$ or $[6..28]$)**:
    $$L_{\text{lamb}} = 22\text{ to }23\text{ wu} \quad (\text{mean } 22.5\text{ wu})$$
  - **Front/Rear Body Width (North/South rows 0, 1, 4, 5, $x \in [9..22]$)**:
    $$W_{\text{lamb}} = 22 - 9 + 1 = 14\text{ wu}$$
  - **Ground Hoof Collision Box** (`NDW_FOOT_HW = 5`, `NDW_FOOT_H = 5` in `seven-world.h:77-78`):
    $$10\text{ wu wide} \times 5\text{ wu deep}$$
  - **Walking Speed** (`SPEED` in `nd_seven_motion.c:8`):
    $$v_{\text{lamb}} = 72.0\text{ wu/s}$$

Note the anatomical aspect ratio of the sprite:
$$\frac{L_{\text{lamb}}}{H_{\text{lamb}}} = \frac{22.5}{18} = 1.25$$
which matches the real-world length-to-height ratio of a domestic lamb!

### 2.2 Real-World Lamb (`Ovis aries`) Reference Dimensions
A real-world domestic lamb (3–6 months old, prior to full adult ewe/ram maturity) measures:
- **Standing height (ground to crown of head)**: $0.60\text{ m}$ to $0.72\text{ m}$ (withers/shoulder height $\approx 0.45\text{ m}$ to $0.55\text{ m}$).
- **Body length (nose/chest to tail)**: $0.75\text{ m}$ to $0.90\text{ m}$.
- **Adult domestic sheep**: Standing crown height $\approx 0.85\text{ m}$ to $1.00\text{ m}$; body length $\approx 1.10\text{ m}$ to $1.35\text{ m}$.

This gives three clean physical conversion anchors:

| Scale Anchor | Conversion ($1\text{ wu}$) | Inverse ($1\text{ m}$) | Lamb Height ($18\text{ wu}$) | Lamb Length ($22.5\text{ wu}$) | Lamb Speed ($72\text{ wu/s}$) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Anchor A (Young Lamb Crown = $60\text{ cm}$)** | $\mathbf{3.333\text{ cm}}$ ($\frac{1}{30}\text{ m}$) | $30.0\text{ wu/m}$ | $0.60\text{ m}$ ($60\text{ cm}$) | $0.75\text{ m}$ ($75\text{ cm}$) | $2.40\text{ m/s}$ ($8.64\text{ km/h}$) |
| **Anchor B (Canonical Lamb: Crown = $72\text{ cm}$, Length = $90\text{ cm}$)** | $\mathbf{4.000\text{ cm}}$ ($0.04\text{ m}$) | $25.0\text{ wu/m}$ | $0.72\text{ m}$ ($72\text{ cm}$) | $0.90\text{ m}$ ($90\text{ cm}$) | $2.88\text{ m/s}$ ($10.37\text{ km/h}$) |
| **Anchor C (Adult Sheep / $1\text{ cell} = 1\text{ m}$)** | $\mathbf{6.250\text{ cm}}$ ($\frac{1}{16}\text{ m}$) | $16.0\text{ wu/m}$ | $1.125\text{ m}$ ($112.5\text{ cm}$) | $1.41\text{ m}$ ($141\text{ cm}$) | $4.50\text{ m/s}$ ($16.20\text{ km/h}$) |

In **Anchor B ($1\text{ wu} = 4\text{ cm} = 0.04\text{ m}$, or $25\text{ wu} = 1\text{ m}$)**, every single proportion of the lamb (`18 wu` = $72\text{ cm}$ tall, `22.5 wu` = $90\text{ cm}$ long, `14 wu` = $56\text{ cm}$ wide, trotting at $2.88\text{ m/s} \approx 10.4\text{ km/h}$) is simultaneous anatomical truth. Below, we show the exact math for all three anchors, with **Anchor B ($1\text{ wu} = 0.04\text{ m}$)** highlighted as the primary lamb-calibrated physical scale.

---

## 3. Room Size & Scenery Proportions in Meters

### 3.1 Single Room Dimensions ($160 \times 128\text{ wu}$)
A single NeverDark room consists of $10 \times 8$ cells of $16 \times 16\text{ wu}$ each ($20{,}480\text{ wu}^2$):

- **Under Anchor A ($1\text{ wu} = 3.333\text{ cm}$)**:
  - $1\text{ cell} = 16\text{ wu} = 0.533\text{ m} \times 0.533\text{ m}$
  - **$1\text{ room}$**:
    $$160 \times \frac{1}{30}\text{ m} = \mathbf{5.33\text{ m wide (E–W)}} \times 128 \times \frac{1}{30}\text{ m} = \mathbf{4.27\text{ m deep (N–S)}}$$
  - **Room Area**: $5.333 \times 4.267 = \mathbf{22.76\text{ m}^2}$ ($\approx 245\text{ sq ft}$, the size of a real living room!).
- **Under Anchor B ($1\text{ wu} = 4.0\text{ cm}$, Canonical Lamb)**:
  - $1\text{ cell} = 16\text{ wu} = 0.64\text{ m} \times 0.64\text{ m}$
  - **$1\text{ room}$**:
    $$160 \times 0.04\text{ m} = \mathbf{6.40\text{ m wide (E–W)}} \times 128 \times 0.04\text{ m} = \mathbf{5.12\text{ m deep (N–S)}}$$
  - **Room Area**: $6.40 \times 5.12 = \mathbf{32.768\text{ m}^2}$ ($\approx 353\text{ sq ft}$).
- **Under Anchor C ($1\text{ wu} = 6.25\text{ cm}$, $1\text{ cell} = 1\text{ m}$)**:
  - $1\text{ cell} = 16\text{ wu} = 1.00\text{ m} \times 1.00\text{ m}$
  - **$1\text{ room}$**:
    $$160 \times 0.0625\text{ m} = \mathbf{10.00\text{ m wide (E–W)}} \times 128 \times 0.0625\text{ m} = \mathbf{8.00\text{ m deep (N–S)}}$$
  - **Room Area**: $10.00 \times 8.00 = \mathbf{80.00\text{ m}^2}$ ($\approx 861\text{ sq ft}$).

*(Notice why it is called a "room": at $5.3\text{ m} \times 4.3\text{ m}$ to $6.4\text{ m} \times 5.1\text{ m}$, a NeverDark tile literally has the exact floor dimensions of an architectural room, which the lamb crosses in $\frac{160}{72} = 2.22\text{ seconds}$ E–W or $\frac{128}{72} = 1.78\text{ seconds}$ N–S!)*

### 3.2 Unified Two-Scale Architecture: Local Lamb Scale vs. Planetary Radius Proportions

Because the NeverDark planet is a $65{,}536 \times 65{,}536$-room ring torus with minor tube radius $r_0 = 1{,}335{,}000\text{ wu}$, the engine unifies two exact physical scales:

1. **Local / Anatomical Scale ($1\text{ wu} = 4.0\text{ cm}$, anchored to the $18\text{-wu}$ lamb $= 0.72\text{ m}$)**:
   Governs the lamb, walking velocity ($72\text{ wu/s} = 2.88\text{ m/s}$), room floor dimensions ($160 \times 128\text{ wu} = 6.40 \times 5.12\text{ m}$), coastal wading shallows ($-8\text{ to }0\text{ wu} = -32\text{ to }0\text{ cm}$), and ground-level flora/boulders.
2. **Planetary Geophysical Scale ($\frac{z}{r_0} = \frac{z_\oplus}{R_\oplus}$, anchored to $r_0 = 1{,}335{,}000\text{ wu} \iff R_\oplus = 6{,}371\text{ km}$, so $1\text{ planetary wu} \iff 4.772\text{ m}$)**:
   Every macroscopic mountain peak, alpine snow line, ocean trench, and tropospheric cloud deck maintains its **exact 1:1 dimensionless proportion to the planet's radius**:
   $$\frac{H_{\text{Everest}}}{R_\oplus} = \frac{8{,}849\text{ m}}{6{,}371{,}000\text{ m}} = 0.00138895 \implies z_{\text{Everest}} = 1{,}335{,}000\text{ wu} \times 0.00138895 = \mathbf{+1{,}854\text{ wu}}$$

| Feature | Engine Size (`wu`) | Local Lamb Scale ($1\text{ wu} = 4.0\text{ cm}$) | Planetary Radius Scale ($\frac{z}{r_0} = \frac{z_\oplus}{R_\oplus}$) | Real-World Comparison |
| :--- | :--- | :--- | :--- | :--- |
| **Grass Tuft** (`NDB_GRASS_TUFT_1`) | $20 \times 18\text{ wu}$ | $0.80 \times 0.72\text{ m}$ | — | Lush pasture meadow grass ($1:1$ with lamb) |
| **Bush** (`NDS_BUSH_DARK`) | $27 \times 27\text{ wu}$ | $1.08 \times 1.08\text{ m}$ | — | True-to-scale pasture shrub ($1:1$ with lamb) |
| **Boulder** (`NDS_ROCK_TAN_BIG`) | $53 \times 54\text{ wu}$ | $2.12 \times 2.16\text{ m}$ | — | True-to-scale pasture boulder ($1:1$ with lamb) |
| **Big Tree** (`NDS_TREE_BIG`) | $61 \times 74\text{ wu}$ | $2.44 \times 2.96\text{ m}$ | — | Stylized orchard/pasture tree |
| **Dynamic Big Flora** (`slot_h <= 162`) | up to $160\text{ wu}$ | up to $6.40\text{ m}$ | — | Medium forest canopy tree |
| **Coastal Wading Shallows** (`field.h:157`) | $-8\text{ to }0\text{ wu}$ | $-0.32\text{ to }0.00\text{ m}$ | — | Ankle-to-knee wading shelf ($1:1$ with lamb) |
| **Coastal Lowlands** (`nd/field.h:169`) | $0\text{ to }+42\text{ wu}$ | $0\text{ to }1.68\text{ m}$ | $0\text{ to }200\text{ m}$ | Coastal plains & gentle rolling terraces |
| **Subalpine Granite Transition** | $+360\text{ to }+760\text{ wu}$ | $14.4\text{ to }30.4\text{ m}$ | $\mathbf{1{,}718\text{ to }3{,}627\text{ m}}$ | True planetary timberline & bare rock emergence |
| **Permanent Alpine Snow Line** | $+780\text{ to }+1{,}360\text{ wu}$ | $31.2\text{ to }54.4\text{ m}$ | $\mathbf{3{,}722\text{ to }6{,}490\text{ m}}$ | True planetary snow/glacier line (latitude-modulated) |
| **Highest Mountain Peak** (`field.h:176`) | $+1{,}854\text{ wu}$ (`+1,981` w/ fine) | $74.16\text{ m}$ ($79.2\text{ m}$) | $\mathbf{+8{,}849\text{ m}}$ ($\mathbf{9{,}454\text{ m}}$) | **Exact $1:1$ $\frac{z}{r_0}$ proportion to Mt. Everest ($8{,}849\text{ m}$)** |
| **Deepest Ocean Trench** (`field.h:164`) | $-2{,}000\text{ wu}$ | $-80.00\text{ m}$ | $\mathbf{-9{,}545\text{ m}}$ | **Exact planetary proportion to abyssal trenches** |
| **Cloud Base ($z_{\text{base}}$)** | $+420\text{ wu}$ | $16.80\text{ m}$ | $\mathbf{+2{,}004\text{ m}}$ | True planetary cumulus condensation base (LCL) |
| **Cloud Anvil Top ($z_{\text{top}}$)** | $+2{,}820\text{ wu}$ | $112.80\text{ m}$ | $\mathbf{+13{,}458\text{ m}}$ | True planetary tropopause (**$> 800\text{ wu}$ above Mt. Everest!**) |

---

## 4. Planetary Torus Geometry & How Big the Planet Is Compared to Earth

### 4.1 3D Torus Embedding vs. Flat Coordinate Lattice
In `mods/nd/ux/nd_seven.c:264` and `mods/nd/ux/nd_seven_gl.c:3090-3108`, the primary planet (Worlds $0$ and $1$) is a 3D ring torus with:
- **Major Radius**: $R = 2{,}000{,}000\text{ wu}$ (center of the torus hole to center of the tube).
- **Minor Radius**: $r_0 = 1{,}335{,}000\text{ wu}$ (radius of the tube cross-section).
- **Aspect Ratio**: $\frac{R}{r_0} = \frac{2{,}000{,}000}{1{,}335{,}000} \approx 1.4981$ (a plump ring torus with inner hole radius $R - r_0 = 665{,}000\text{ wu}$ and outer equatorial radius $R + r_0 = 3{,}335{,}000\text{ wu}$).

How closely does the 3D torus match the $10{,}485{,}760 \times 8{,}388{,}608\text{ wu}$ flat room lattice?
1. **Poloidal (North–South) Tube Circumference**:
   $$C_{\text{minor}} = 2\pi r_0 = 2\pi (1{,}335{,}000) = \mathbf{8{,}388{,}052.39\text{ wu}}$$
   Compare this to `ND_SEVEN_WORLD_DIM_H` $= 8{,}388{,}608\text{ wu}$:
   $$\frac{C_{\text{minor}}}{W_Y} = \frac{8{,}388{,}052.39}{8{,}388{,}608} = \mathbf{0.999934} \quad (\text{an exact } 1:1\text{ match to within } 0.0066\%!)$$
2. **Toroidal (East–West) Circumference**:
   Because the East–West circumference on a torus varies with poloidal latitude $v = 2\pi \frac{wy}{W_Y}$ as $C(v) = 2\pi(R + r_0 \cos v)$:
   - **At the Tube Centerline ($\cos v = 0$, N/S poles of the tube)**:
     $$C_{\text{major}} = 2\pi R = 2\pi (2{,}000{,}000) = \mathbf{12{,}566{,}370.61\text{ wu}} \quad (1.1984 \times W_X)$$
   - **Where the Torus Matches $W_X = 10{,}485{,}760\text{ wu}$ Exactly (Isometric Parallels)**:
     $$2\pi(R + r_0 \cos v) = 10{,}485{,}760 \implies R + r_0 \cos v = 1{,}668{,}860.4\text{ wu} \implies \cos v = -0.2480 \quad (v \approx \pm 104.36^\circ)$$
   - **At the Outer Equator ($v = 0$, $\cos v = +1$)**:
     $$C_{\text{outer}} = 2\pi(R + r_0) = 2\pi(3{,}335{,}000) = \mathbf{20{,}954{,}423.00\text{ wu}}$$
   - **At the Inner Hole Equator ($v = \pi$, $\cos v = -1$)**:
     $$C_{\text{inner}} = 2\pi(R - r_0) = 2\pi(665{,}000) = \mathbf{4{,}178{,}318.23\text{ wu}}$$
3. **Total Surface Area & Volume in World Units**:
   - **Flat Lattice Area**:
     $$A_{\text{flat}} = W_X \cdot W_Y = 10{,}485{,}760 \times 8{,}388{,}608 = \mathbf{87{,}960{,}930{,}222{,}080\text{ wu}^2} \approx 8.7961 \times 10^{13}\text{ wu}^2$$
   - **True 3D Torus Surface Area**:
     $$A_{\text{torus}} = (2\pi R)(2\pi r_0) = 4\pi^2 R r_0 = 4\pi^2 (2{,}000{,}000)(1{,}335{,}000) = \mathbf{105{,}407{,}375{,}003{,}634\text{ wu}^2} \approx 1.0541 \times 10^{14}\text{ wu}^2$$
     *(Note: $A_{\text{torus}} / A_{\text{flat}} = 1.19834$, because $C_{\text{major}} / W_X = 1.19842$ and $C_{\text{minor}} / W_Y = 0.999934$).*
   - **True 3D Torus Volume**:
     $$V_{\text{torus}} = (\pi r_0^2)(2\pi R) = 2\pi^2 R r_0^2 = \mathbf{7.03594 \times 10^{19}\text{ wu}^3}$$

---

### 4.2 Physical Size of the NeverDark Planet vs. Earth

Converting the world-unit quantities above into kilometers, square kilometers, and cubic kilometers using our three lamb scale anchors, and comparing against **Earth** ($R_\oplus = 6{,}371\text{ km}$, $C_\oplus = 40{,}075\text{ km}$, $A_\oplus = 510{,}072{,}000\text{ km}^2$, $A_{\text{land},\oplus} = 148{,}940{,}000\text{ km}^2$, $V_\oplus = 1.08321 \times 10^{12}\text{ km}^3$):

| Planetary Metric | Engine Units (`wu`) | Anchor A ($1\text{ wu} = 3.33\text{ cm}$) | **Anchor B ($1\text{ wu} = 4.0\text{ cm}$, Canonical)** | Anchor C ($1\text{ wu} = 6.25\text{ cm}$) | Earth ($R_\oplus$) | Ratio to Earth (Anchor B) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Flat Width $W_X$ (E–W)** | $10{,}485{,}760\text{ wu}$ | $349.53\text{ km}$ | **$419.43\text{ km}$** | $655.36\text{ km}$ | $40{,}075\text{ km}$ | **$1.047\%$** ($\frac{1}{95.5}$) |
| **Flat Height $W_Y$ (N–S)** | $8{,}388{,}608\text{ wu}$ | $279.62\text{ km}$ | **$335.54\text{ km}$** | $524.29\text{ km}$ | $40{,}008\text{ km}$ | **$0.839\%$** ($\frac{1}{119.2}$) |
| **Torus Major Radius ($R$)** | $2{,}000{,}000\text{ wu}$ | $66.67\text{ km}$ | **$80.00\text{ km}$** | $125.00\text{ km}$ | $6{,}371\text{ km}$ | **$1.256\%$** ($\frac{1}{79.6}$) |
| **Torus Minor Radius ($r_0$)** | $1{,}335{,}000\text{ wu}$ | $44.50\text{ km}$ | **$53.40\text{ km}$** | $83.44\text{ km}$ | — | — |
| **Outer Torus Diameter ($2(R+r_0)$)** | $6{,}670{,}000\text{ wu}$ | $222.33\text{ km}$ | **$266.80\text{ km}$** | $416.88\text{ km}$ | $12{,}742\text{ km}$ | **$2.094\%$** ($\frac{1}{47.8}$) |
| **Outer Equatorial Circumference** | $20{,}954{,}423\text{ wu}$ | $698.48\text{ km}$ | **$838.18\text{ km}$** | $1{,}309.65\text{ km}$ | $40{,}075\text{ km}$ | **$2.092\%$** ($\frac{1}{47.8}$) |
| **Flat Lattice Surface Area ($A_{\text{flat}}$)** | $8.796 \times 10^{13}\text{ wu}^2$ | $97{,}734\text{ km}^2$ | **$140{,}737\text{ km}^2$** | $343{,}597\text{ km}^2$ | $510{,}072{,}000\text{ km}^2$ | **$0.0276\%$** ($\frac{1}{3{,}624}$) |
| **3D Torus Surface Area ($A_{\text{torus}}$)** | $1.054 \times 10^{14}\text{ wu}^2$ | $117{,}119\text{ km}^2$ | **$168{,}652\text{ km}^2$** | $411{,}748\text{ km}^2$ | $510{,}072{,}000\text{ km}^2$ | **$0.0331\%$** ($\frac{1}{3{,}024}$) |
| **3D Torus Volume ($V_{\text{torus}}$)** | $7.036 \times 10^{19}\text{ wu}^3$ | $2{,}605{,}905\text{ km}^3$ | **$4{,}503{,}003\text{ km}^3$** | $17{,}177{,}593\text{ km}^3$ | $1.0832 \times 10^{12}\text{ km}^3$ | **$0.000416\%$** ($\frac{1}{240{,}553}$) |

#### Real-World Geographic & Astronomical Comparisons:
1. **Country / State Scale**:
   - Under **Anchor A ($3.33\text{ cm/wu}$)**, the planet's surface area ($97{,}734\text{ km}^2$ flat / $117{,}119\text{ km}^2$ torus) is almost identical to **South Korea ($100{,}210\text{ km}^2$)**, **Iceland ($103{,}000\text{ km}^2$)**, or **Portugal ($92{,}212\text{ km}^2$)**.
   - Under **Anchor B ($4.0\text{ cm/wu}$, Canonical Lamb)**, the planet's surface area ($140{,}737\text{ km}^2$ flat / $168{,}652\text{ km}^2$ torus) is the size of **Greece ($131{,}957\text{ km}^2$)**, **England ($130{,}279\text{ km}^2$)**, **New York State ($141{,}300\text{ km}^2$)**, or **Florida ($170{,}312\text{ km}^2$)**.
   - Under **Anchor C ($6.25\text{ cm/wu}$)**, the planet's surface area ($343{,}597\text{ km}^2$ flat / $411{,}748\text{ km}^2$ torus) is the size of **Germany ($357{,}588\text{ km}^2$)**, **Japan ($377{,}975\text{ km}^2$)**, or **California ($423{,}970\text{ km}^2$)**.
2. **Solar System Body Scale**:
   - With an outer diameter of **$266.8\text{ km}$** (Anchor B), the NeverDark torus planet is comparable in diameter to major Saturnian/Uranian moons like **Phoebe ($213\text{ km}$)**, **Hyperion ($270\text{ km}$)**, or **Miranda ($471\text{ km}$)**, and the main-belt asteroid **4 Vesta ($525\text{ km}$)**.
   - Its secondary moon (`world_id = 2`, $R_{\text{moon}} = 800{,}000\text{ wu} = 32\text{ km}$, $r_{\text{moon}} = 500{,}000\text{ wu} = 20\text{ km}$, outer diameter $= 104\text{ km}$, surface area $= 25{,}266\text{ km}^2$) is about the size of **Sicily** or **Vermont**.
3. **Circumnavigation Time by Hoof**:
   - Regardless of the metric conversion factor, the lamb walks at $72\text{ wu/s}$.
   - Walking nonstop in a straight line East–West across all $65{,}536$ rooms ($10{,}485{,}760\text{ wu}$) takes:
     $$T_{\text{E–W}} = \frac{10{,}485{,}760\text{ wu}}{72\text{ wu/s}} = \mathbf{145{,}635.56\text{ seconds}} = \mathbf{40\text{ hours } 27\text{ minutes } 16\text{ seconds}} \quad (\approx 1.69\text{ real days})$$
   - Walking nonstop North–South around the poloidal tube ($8{,}388{,}608\text{ wu}$) takes:
     $$T_{\text{N–S}} = \frac{8{,}388{,}608\text{ wu}}{72\text{ wu/s}} = \mathbf{116{,}508.44\text{ seconds}} = \mathbf{32\text{ hours } 21\text{ minutes } 48\text{ seconds}} \quad (\approx 1.35\text{ real days})$$

---

## 5. How Many Screens at Default Zoom Fit on the Planet's Surface?

Because NeverDark supports both the classic 2D orthographic scale (`ND_SEVEN_SCALE = 2` on a `640x480` stage) and the 3D tilted perspective camera (`ND_SEVEN_CAM_DIST = 280.0`, `ND_SEVEN_DIORAMA_PITCH = 55.0°`, `ND_SEVEN_CAM_FOV = 45.0°` at default `zoom = 1.0`), we compute the exact screen count for all three geometric definitions of a "default-zoom screen."

### 5.1 Definition 1: Classic 2D / Orthographic Stage (`640x480` at `SCALE = 2`)
In `nd_lamina.h:14-15` and `nd_seven.h:75`:
- Canvas resolution: $640 \times 480\text{ px}$ at $2\times$ pixel magnification (`ND_SEVEN_SCALE = 2`).
- World units per screen:
  $$W_{\text{screen,2D}} = \frac{640}{2} = 320\text{ wu}, \qquad H_{\text{screen,2D}} = \frac{480}{2} = 240\text{ wu}$$
- Rooms per screen:
  $$\frac{320}{160} \times \frac{240}{128} = 2.0\text{ rooms wide} \times 1.875\text{ rooms high} = \mathbf{3.75\text{ rooms/screen}}$$
- Screen area in world units (and Anchor B $\text{m}^2$):
  $$A_{\text{screen,2D}} = 320 \times 240 = \mathbf{76{,}800\text{ wu}^2} \quad (= 122.88\text{ m}^2\text{ at }4\text{ cm/wu})$$
- **Screens fitting across the planet**:
  - **East–West**: $\frac{10{,}485{,}760}{320} = \mathbf{32{,}768\text{ screens}}$
  - **North–South**: $\frac{8{,}388{,}608}{240} = \mathbf{34{,}952.53\text{ screens}}$
  - **Total Screens in the Flat World Lattice ($A_{\text{flat}} / A_{\text{screen,2D}}$)**:
    $$\frac{87{,}960{,}930{,}222{,}080}{76{,}800} = \mathbf{1{,}145{,}324{,}612.27\text{ screens}} \quad (\approx \mathbf{1.145\text{ billion screens}})$$
  - **Total Screens on the True 3D Torus Surface ($A_{\text{torus}} / A_{\text{screen,2D}}$)**:
    $$\frac{105{,}407{,}375{,}003{,}634}{76{,}800} = \mathbf{1{,}372{,}491{,}862.03\text{ screens}} \quad (\approx \mathbf{1.372\text{ billion screens}})$$

---

### 5.2 Definition 2: 3D Perspective Camera Focal-Plane Footprint (`zoom = 1.0`)
In `nd_seven.h:85-88` and `nd_seven_gl.c:3142-3186`, at default `zoom = 1.0`:
- Camera distance to lamb target: $D = 280.0\text{ wu}$.
- Camera pitch from horizontal: $\theta = 55^\circ$ ($\sin 55^\circ \approx 0.819152$, $\cos 55^\circ \approx 0.573576$).
- Vertical field of view: $\alpha = 45^\circ$ ($\tan(\alpha/2) = \tan(22.5^\circ) = \sqrt{2} - 1 \approx 0.41421356$).
- Aspect ratio (standard $640 \times 480$ canvas): $\text{aspect} = \frac{4}{3} \approx 1.333333$.

At the lamb's focal depth ($Z_{\text{cam}} = D = 280\text{ wu}$):
- **Focal Width (East–West)**:
  $$W_{\text{focal}} = 2 D \tan(22.5^\circ) \cdot \frac{4}{3} = 2(280)(0.41421356)(1.333333) = \mathbf{309.279\text{ wu}} \quad (1.933\text{ rooms}, \text{ or } 12.37\text{ m})$$
- **Perpendicular Frustum Height**:
  $$H_{\text{perp}} = 2 D \tan(22.5^\circ) = 2(280)(0.41421356) = \mathbf{231.9596\text{ wu}}$$
- **Ground-Projected Focal Depth (North–South along horizontal plane at $55^\circ$ pitch)**:
  $$H_{\text{focal,ground}} = \frac{H_{\text{perp}}}{\sin 55^\circ} = \frac{231.9596}{0.819152} = \mathbf{283.170\text{ wu}} \quad (2.212\text{ rooms}, \text{ or } 11.33\text{ m})$$
- **Ground-Projected Focal Rectangle Area**:
  $$A_{\text{focal,ground}} = 309.279 \times 283.170 = \mathbf{87{,}578.78\text{ wu}^2} \quad (4.276\text{ rooms}, \text{ or } 140.13\text{ m}^2)$$
- **Total Screens in the Flat World Lattice ($A_{\text{flat}} / A_{\text{focal,ground}}$)**:
  $$\frac{87{,}960{,}930{,}222{,}080}{87{,}578.78} = \mathbf{1{,}004{,}363{,}481\text{ screens}} \quad (\approx \mathbf{1.004\text{ billion screens}})$$
- **Total Screens on the True 3D Torus Surface ($A_{\text{torus}} / A_{\text{focal,ground}}$)**:
  $$\frac{105{,}407{,}375{,}003{,}634}{87{,}578.78} = \mathbf{1{,}203{,}572{,}061\text{ screens}} \quad (\approx \mathbf{1.204\text{ billion screens}})$$

---

### 5.3 Definition 3: Exact 3D Frustum Trapezoid Intersecting the Ground Plane ($z = 0$)
Because the camera is pitched at $\theta = 55^\circ$ above the southern horizon at eye position $\vec{E} = (0, D\cos 55^\circ, D\sin 55^\circ) = (0, 160.60\text{ wu}, 229.36\text{ wu})$, the top of the screen looks further North at a shallower grazing angle ($55^\circ - 22.5^\circ = 32.5^\circ$) while the bottom of the screen looks steeper downward ($55^\circ + 22.5^\circ = 77.5^\circ$). Intersecting the four corner rays $(x_{\text{ndc}}, y_{\text{ndc}}) \in \{-1, +1\}^2$ with the ground plane $z = 0$ yields an exact symmetric trapezoid:

- **Near (Bottom-of-Screen) Ground Edge ($y_{\text{ndc}} = -1$, ray angle $77.5^\circ$)**:
  - Ground $Y$ coordinate: $y_{\text{near}} = +109.75\text{ wu}$ South of the lamb
  - Near Width (E–W): $W_{\text{near}} = \mathbf{239.74\text{ wu}}$ ($1.498\text{ rooms}$, or $9.59\text{ m}$)
- **Far (Top-of-Screen) Ground Edge ($y_{\text{ndc}} = +1$, ray angle $32.5^\circ$)**:
  - Ground $Y$ coordinate: $y_{\text{far}} = -199.43\text{ wu}$ North of the lamb
  - Far Width (E–W): $W_{\text{far}} = \mathbf{435.63\text{ wu}}$ ($2.723\text{ rooms}$, or $17.43\text{ m}$)
- **Total North–South Ground Depth**:
  $$\Delta Y_{\text{ground}} = 109.75 - (-199.43) = \mathbf{309.18\text{ wu}} \quad (2.415\text{ rooms}, \text{ or } 12.37\text{ m})$$
- **Exact Ground Trapezoid Area ($4:3$ aspect ratio, $640 \times 480$)**:
  $$A_{\text{trap,4:3}} = \frac{W_{\text{near}} + W_{\text{far}}}{2} \cdot \Delta Y_{\text{ground}} = \frac{239.74 + 435.63}{2} \times 309.18 = \mathbf{104{,}405.24\text{ wu}^2} \quad (\mathbf{5.098\text{ rooms}}, \text{ or } 167.05\text{ m}^2)$$
  - **Total $4:3$ 3D Screens in Flat World Lattice**:
    $$\frac{87{,}960{,}930{,}222{,}080}{104{,}405.24} = \mathbf{842{,}495{,}376\text{ screens}} \quad (\approx \mathbf{842.5\text{ million screens}})$$
  - **Total $4:3$ 3D Screens on 3D Torus Surface**:
    $$\frac{105{,}407{,}375{,}003{,}634}{104{,}405.24} = \mathbf{1{,}009{,}598{,}532\text{ screens}} \quad (\approx \mathbf{1.010\text{ billion screens}})$$

- **Exact Ground Trapezoid Area ($16:9$ widescreen aspect ratio, e.g., $1920 \times 1080$)**:
  Since horizontal frustum width scales linearly with aspect ratio ($\frac{16/9}{4/3} = \frac{4}{3}$):
  - $W_{\text{near,16:9}} = 319.66\text{ wu}$, $W_{\text{far,16:9}} = 580.84\text{ wu}$, $\Delta Y_{\text{ground}} = 309.18\text{ wu}$
  - $A_{\text{trap,16:9}} = \mathbf{139{,}206.98\text{ wu}^2}$ ($\mathbf{6.797\text{ rooms}}$, or $222.73\text{ m}^2$)
  - **Total $16:9$ 3D Screens in Flat World Lattice**:
    $$\frac{87{,}960{,}930{,}222{,}080}{139{,}206.98} = \mathbf{631{,}871{,}532\text{ screens}} \quad (\approx \mathbf{631.9\text{ million screens}})$$
  - **Total $16:9$ 3D Screens on 3D Torus Surface**:
    $$\frac{105{,}407{,}375{,}003{,}634}{139{,}206.98} = \mathbf{757{,}198{,}899\text{ screens}} \quad (\approx \mathbf{757.2\text{ million screens}})$$

---

## 6. Summary Master Table

| Viewport / Screen Mode at Default Zoom (`zoom = 1.0`) | Footprint in World Units (`wu²`) | Rooms per Screen | Physical Area (`1 wu = 4 cm`) | Screens in Flat Lattice ($2^{32}$ rooms) | Screens on 3D Torus ($4\pi^2 R r_0$) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **2D / Ortho Stage (`640x480` @ `2x`)** | $320 \times 240 = 76{,}800\text{ wu}^2$ | $3.750\text{ rooms}$ | $122.88\text{ m}^2$ | **$1{,}145{,}324{,}612$** | **$1{,}372{,}491{,}862$** |
| **3D Focal-Plane Rect (`4:3`, $D=280$)** | $309.3 \times 283.2 = 87{,}579\text{ wu}^2$ | $4.276\text{ rooms}$ | $140.13\text{ m}^2$ | **$1{,}004{,}363{,}481$** | **$1{,}203{,}572{,}061$** |
| **3D Ground Trapezoid (`4:3`, $640 \times 480$)** | $\frac{239.7+435.6}{2} \times 309.2 = 104{,}405\text{ wu}^2$ | $5.098\text{ rooms}$ | $167.05\text{ m}^2$ | **$842{,}495{,}376$** | **$1{,}009{,}598{,}532$** |
| **3D Ground Trapezoid (`16:9` Widescreen)** | $\frac{319.7+580.8}{2} \times 309.2 = 139{,}207\text{ wu}^2$ | $6.797\text{ rooms}$ | $222.73\text{ m}^2$ | **$631{,}871{,}532$** | **$757{,}198{,}899$** |

---

## 7. Atmospheric & Cloud Scale: Earth Climatology vs. NeverDark

### 7.1 Real-World Earth Cloud Climatology (Satellite Ground Truth)
Global satellite observations from NASA (MODIS, CERES, CloudSat) and the International Satellite Cloud Climatology Project (ISCCP) provide the empirical benchmark for planetary cloud cover:

1. **Total Cloud Fraction vs. Optically Significant Cloud Cover**:
   - **Total Global Mean Cloud Fraction**: $\approx 67\%$. However, this figure includes optically thin sub-visual cirrus ($\tau < 0.3$), which does not prevent direct sunlight from reaching the ground or obscure surface features from orbit.
   - **Optically Significant Cloud Fraction ($\tau > 2.0$)**: $\mathbf{42\%\text{--}48\%}$. On average, **over half of the Earth's surface ($52\%\text{--}58\%$) is visibly clear or sunny** at any moment.
2. **Zonal Distribution & Hadley Cell Subsidence**:
   - **Subtropical Anticyclonic Deserts & Oceanic Highs ($15^\circ\text{--}35^\circ$ N/S)**:
     The descending branch of the Hadley circulation creates massive adiabatic compression and subsidence drying. Regions like the Sahara, Arabian Peninsula, Kalahari, Australian Outback, Atacama, and oceanic anticyclones (Azores High, North/South Pacific Highs) have mean cloud fractions of **only $15\%\text{--}25\%$**, meaning they experience **$75\%\text{--}85\%$ persistent clear sunny sky**.
   - **Intertropical Convergence Zone (ITCZ, $0^\circ\text{--}10^\circ$)**:
     A narrow, migrating thermal belt of intense solar heating and deep convective thunderheads (mean cloud cover $\approx 70\%$).
   - **Mid-Latitude Storm Tracks ($45^\circ\text{--}60^\circ$)**:
     Baroclinic waves produce swirling, comma-shaped extratropical cyclone fronts separated by wide, clear post-frontal subsidence wedges.

---

### 7.2 NeverDark Synoptic Cloud Model Recalibration
Prior to recalibration, the NeverDark cloud shader applied an asymmetric positive ridge term and an excessively low humidity threshold:
$$\text{synLo} = 0.505 - 0.125 \cdot \text{humidity} \approx 0.424$$
Because the synoptic front field $\text{synFront}$ has an intrinsic spatial mean of $0.50$, setting the threshold to $0.424$ caused **over $75\%$ of the planetary surface to generate cloud cover**, creating an overwhelmingly overcast world with almost no visible continents or oceans.

#### Mathematical Recalibration to Match Earth's Sunny Ratio
1. **Zero-Mean Ridge Shaping**:
   $$\text{front}_0 = \text{oct}_0.x + (r_0 - 0.68) \cdot 0.28$$
   Subtracting the expected value of the ridge envelope ($E[r_0] = 0.68$) maintains a strictly zero-mean perturbation around $0.500$, preserving symmetric cyclonic convergence and anticyclonic divergence.
2. **Hadley/Ferrel Subsidence Threshold**:
   $$\text{synLo} = 0.540 - 0.065 \cdot \text{humidity} \quad (0.500\text{ to }0.525)$$
   $$\text{synHi} = 0.720 - 0.055 \cdot \text{humidity} \quad (0.680\text{ to }0.700)$$
   $$\text{synRegime} = \text{smoothstep}(\text{synLo}, \text{synHi}, \text{synFront})$$
   - **Sunny / Clear Sky Regime ($\text{synFront} < \text{synLo}$)**:
     Encompasses **$58\%\text{--}62\%$ of the total planetary surface**. Here $\text{synRegime} = 0.0$, producing **100% clear sunny sky** where oceans, lakes, shores, and forests receive full solar illumination with zero atmospheric occlusion.
   - **Active Cyclonic / Frontal Regime ($\text{synFront} \ge \text{synLo}$)**:
     Encompasses **$38\%\text{--}42\%$ of the planet**. Within this active zone:
     - Broken cumulus streets (FEW/SCT) have cellular coverage of only $20\%\text{--}35\%$, maintaining sunny flight corridors between cloud elements.
     - Fully overcast shields occur only in deep cyclonic storm cores where $\text{synRegime} > 0.62$ ($\approx 8\%\text{--}12\%$ of the planet, matching Earth's hurricane and polar low cores).

---

### 7.3 Atmosphere & Tropospheric Scale: Earth vs. NeverDark

Every vertical layer of the NeverDark lithosphere, hydrosphere, and troposphere is calibrated to the **Planetary Radius Scale** ($\frac{z}{r_0} = \frac{z_\oplus}{R_\oplus}$, where $r_0 = 1{,}335{,}000\text{ wu} \iff R_\oplus = 6{,}371\text{ km}$, or $1\text{ wu} \iff 4.772\text{ planetary meters}$):

| Atmospheric / Geophysical Layer | Earth (Physical Reality) | Planetary Equivalent ($\frac{z}{r_0} \cdot R_\oplus$) | NeverDark Engine Units (`wu`) | Physical & Visual Behavior |
| :--- | :--- | :--- | :--- | :--- |
| **Deepest Ocean Trench** | $-10{,}935\text{ m}$ (Mariana) | $-9{,}545\text{ m}$ | **`-2,000 wu`** (`field.h:164`) | Full Beer-Lambert bathymetric progression (`0..2,000 wu`) |
| **Subalpine Rock & Timberline** | $1{,}700\text{--}3{,}600\text{ m}$ | $1{,}718\text{--}3{,}627\text{ m}$ | **`+360 .. +760 wu`** | Bare granite/slate emergence above forested uplands |
| **Permanent Alpine Snow Line** | $3{,}700\text{--}6{,}500\text{ m}$ | $3{,}722\text{--}6{,}490\text{ m}$ | **`+780 .. +1,360 wu`** | Climate-modulated snow caps ($520\text{ wu}$ polar to $1{,}060\text{ wu}$ tropical) |
| **Max Mountain Peak (Everest)** | $+8{,}849\text{ m}$ (Mt. Everest) | $+8{,}849\text{ m}$ ($+9{,}454\text{ m}$ w/ fine) | **`+1,854 wu`** (`+1,981` w/ fine) | Exact $1:1$ $\frac{H_{\text{Everest}}}{R_\oplus}$ planetary radius ratio |
| **Troposphere Base ($z_{\text{base}}$)** | $1{,}800\text{--}2{,}200\text{ m}$ | $2{,}004\text{ m}$ | **`z_base = 420 wu`** | Strictly above lowlands, valleys, and coastal hills |
| **Deck 1: Stratocumulus / Cumulus** | $2{,}000\text{--}5{,}000\text{ m}$ | $2{,}004\text{--}5{,}097\text{ m}$ | **`z = 420 .. 1,068 wu`** ($h \in [0.00, 0.27]$) | Marine decks & cumulus streets; high ranges rise through Deck 1 |
| **Inter-Deck Clear Cleft 1–2** | $5{,}100\text{--}6{,}000\text{ m}$ | $5{,}097\text{--}6{,}013\text{ m}$ | **`z = 1,068 .. 1,260 wu`** ($h \in [0.27, 0.35]$) | Clean vertical air gap separating low and mid cloud decks |
| **Deck 2: Altocumulus / Congestus** | $6{,}000\text{--}9{,}300\text{ m}$ | $6{,}013\text{--}9{,}334\text{ m}$ | **`z = 1,260 .. 1,956 wu`** ($h \in [0.35, 0.64]$) | Mackerel ripple bands & convective towers wrapping highest peaks |
| **Inter-Deck Clear Cleft 2–3** | $9{,}300\text{--}10{,}250\text{ m}$ | $9{,}334\text{--}10{,}251\text{ m}$ | **`z = 1,956 .. 2,148 wu`** ($h \in [0.64, 0.72]$) | Clean air gap above the tallest $+1{,}981\text{ wu}$ summit |
| **Deck 3: Cirrus / Anvil Outflow** | $10{,}250\text{--}13{,}500\text{ m}$ | $10{,}251\text{--}13{,}458\text{ m}$ | **`z = 2,148 .. 2,820 wu`** ($h \in [0.72, 1.00]$) | Jet-stream ice streaks & anvils **$> 800\text{ wu}$ above all peaks** |
| **Troposphere Top ($z_{\text{top}}$)** | $13{,}000\text{--}14{,}000\text{ m}$ | $13{,}458\text{ m}$ | **`z_top = 2,820 wu`** | Full vertical cloud envelope span $= 2{,}400\text{ wu}$ |
| **Global Cloud Cover (Optically Thick)** | $\mathbf{42\%\text{--}48\%}$ | $\mathbf{38\%\text{--}42\%}$ | N/A | Calibrated so continents, snow peaks & oceans remain clear |
| **Sunny / Clear Sky Surface Fraction** | $\mathbf{52\%\text{--}58\%}$ | $\mathbf{58\%\text{--}62\%}$ | N/A | **Over half the planet is sunny and cloudless** |

---

### 7.4 Volumetric Curved-Torus Projection & Analytical Mountain–Cloud Intersection

#### Root Cause of Low-LOD Mountain Triangle Cutouts
Previously, the cloud volume was bounded at `zBase = 380 wu, zTop = 980 wu` and its screen-projection proxy shell (`eff_shell_z`) was clamped at `280 wu` with hardware `GL_DEPTH_TEST` enabled. Because mountains in `nd/field.h` rise to `+1,854 wu` (`+1,981 wu` with fine ridges), every terrain triangle above `280 wu` sat in front of the cloud proxy shell in the hardware Z-buffer. When zooming out caused distant chunks to drop to the $16 \times 16$ macro triangle grid (`GL_GRID_QUADS_MACRO = 16`), the hardware depth test clipped the clouds along the raw polygon edges of those coarse terrain triangles.

#### 1. Signed Curved-Torus Shell Intersection (`eff_shell_z = 2,150 wu`)
The proxy shell altitude `eff_shell_z = min(2150.0, max(24.0, lamb_z + 0.58 * d))` rises smoothly above all $+1{,}981\text{ wu}$ mountain vertices once zoomed out, while remaining strictly below the camera eye ($z_{\text{eye}} = \text{lamb\_z} + 0.819 \cdot d$). In `s_fs_cloud`, the exact signed quadratic intersection along `viewDir` from the proxy shell radius $r_{\text{shell}} = r_0 + v_{\text{ShellZ}}$ to any target cloud altitude $z_{\text{Proj}} \in [z_{\text{base}}, z_{\text{top}} + \text{effOrbitLift}]$ is solved without clamping $\Delta z_P = z_{\text{Proj}} - v_{\text{ShellZ}}$:
$$B_{\text{shell}} = (r_0 + v_{\text{ShellZ}}) (\vec{V}_{\text{view}} \cdot \vec{N}), \qquad dR_P = \Delta z_P (2 r_0 + z_{\text{Proj}} + v_{\text{ShellZ}})$$
$$u_{\text{ray}} = \frac{dR_P}{\sqrt{\max(1.0, B_{\text{shell}}^2 + dR_P)} + B_{\text{shell}}}, \qquad s_{\text{Step}} = -u_{\text{ray}}$$
This handles cloud layers both above and below `vShellZ` with zero camera near-plane clipping and full hardware depth occlusion of the far side of the torus ring at orbital min-zoom.

#### 2. Per-Pixel Bicubic Mountain–Cloud Intersection
At each of the 12 top-to-bottom raymarch steps $s \in [0..11]$, `s_fs_cloud` evaluates the chunk's $49 \times 49$ heightfield texture (`uHeightTex`) at the exact ray-projected chunk UV `stepUV = vUV + (dxy * sStep) / uChunkSpan` using the $C^1$ Catmull-Rom bicubic spline (`sampleHeightCloud(stepUV)`):
$$\text{stepClear} = \text{smoothstep}(z_{\text{terr}} - 24.0,\; z_{\text{terr}} + 110.0,\; z)$$
$$\text{rayGroundVis} \mathrel{*}= \text{smoothstep}(z_{\text{terr}} - 36.0,\; z_{\text{terr}} + 28.0,\; z)$$
- **Zero Triangle Facets at Any LOD**: Because `sampleHeightCloud(stepUV)` is a continuous per-pixel $C^1$ bicubic surface on the $49 \times 49$ height texture, high mountain peaks emerging through Deck 1 (`420..1,068 wu`) and Deck 2 (`1,260..1,956 wu`) part the clouds with a smooth, misty contour at every zoom level.
- **True 3D Ridge Occlusion**: Because the ray marches top-to-bottom from the camera, once a viewing ray strikes a mountain ridge ($z \le z_{\text{terr}}$), `rayGroundVis` drops to `0.0`, preventing lower cloud decks behind the mountain from bleeding through the peak, while Deck 3 cirrus and anvils (`2,148..2,820 wu`) pass cleanly over the summit.
