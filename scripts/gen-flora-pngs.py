#!/usr/bin/env python3
"""High-craft 16-bit RPG pixel-art flora generator for NeverDark (nd-plant).

Generates 64 RGBA .png sprites (16 species x 2 stages {small, big} x 2 variants {1, 2} = 4 per species)
by extracting, healing, palette-mapping, and multi-tier compositing the hand-pixelled
foliage crowns, sculpted leaf-clump lobes, gnarled root trunks, conifer needle tiers,
and crop sprites from external/nd-seven/resources/tiles.png.

Key craft & layout invariants:
- Exactly 4 PNGs per species: small/1.png, small/2.png, big/1.png, big/2.png.
- Zero grass tufts or ground blades around tree roots: tree bases are pure sculpted bark
  and spreading buttress roots with transparent surroundings so ground grass can be rendered
  independently by the terrain/stage engine.
- Mandatory strong visual variation between variant 1 and variant 2 within every species and stage:
  distinct canopy architecture (pillow-lobe vs serrated leaf-spray crowns, wide spreading dome vs
  tall multi-leader or asymmetrical wind-swept fork, distinct trunk/root silhouettes, and
  complementary foliage color ramps).
- Every sprite is anchored at bottom-center (width // 2, height - 1) corresponding to (0, 0)
  in ground-plane coordinates.
"""

import glob
import math
import os
import sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TILES_PATH = os.path.join(ROOT, "external", "nd-seven", "resources", "tiles.png")
OUT_DIRS = [
    os.path.join(ROOT, "external", "nd-plant", "art", "plant"),
    os.path.join(ROOT, "htdocs", "nd", "art", "plant"),
]

SPECIES_LIST = [
    "pinus_sylvestris",
    "pseudotsuga_menziesii",
    "betula_pendula",
    "linum_usitatissimum",
    "betula_pubescens",
    "abies_alba",
    "arthrocereus_rondonianus",
    "acacia_senegal",
    "daucus_carota",
    "solanum_lycopersicum",
    "quercus_robur",
    "salix_babylonica",
    "acer_pseudoplatanus",
    "olea_europaea",
    "bellis_perennis",
    "typha_latifolia",
]


def hash2d(x, y, seed):
    h = (x * 374761393 + y * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return (h ^ (h >> 16)) & 0xFFFFFFFF


def is_leaf_green(r, g, b):
    return g > r + 8 and g > b + 6


def is_apple_red(r, g, b):
    return r > g + 18 and r > 90


def keep_largest_component(im, radius=2):
    """Keep only the largest connected component (within radius) to strip stray pixels."""
    w, h = im.size
    px = im.load()
    visited = [[False] * h for _ in range(w)]
    best_comp = []
    for sx in range(w):
        for sy in range(h):
            if visited[sx][sy] or px[sx, sy][3] == 0:
                continue
            q = [(sx, sy)]
            visited[sx][sy] = True
            comp = []
            while q:
                cx, cy = q.pop()
                comp.append((cx, cy))
                for dx in range(-radius, radius + 1):
                    for dy in range(-radius, radius + 1):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < w and 0 <= ny < h and not visited[nx][ny] and px[nx, ny][3] > 0:
                            visited[nx][ny] = True
                            q.append((nx, ny))
            if len(comp) > len(best_comp):
                best_comp = comp
    keep = set(best_comp)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    opx = out.load()
    for x, y in keep:
        opx[x, y] = px[x, y]
    bb = out.getchannel("A").getbbox()
    return out.crop(bb) if bb else out


def extract_clean(sheet, roi, erase_rects=(), keep_main=True):
    im = sheet.crop(roi).copy()
    px = im.load()
    w, h = im.size
    for ex0, ey0, ex1, ey1 in erase_rects:
        for y in range(max(0, ey0), min(h, ey1)):
            for x in range(max(0, ex0), min(w, ex1)):
                px[x, y] = (0, 0, 0, 0)
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 180:
                px[x, y] = (0, 0, 0, 0)
            elif a < 255:
                px[x, y] = (r, g, b, 255)
    if keep_main:
        return keep_largest_component(im, radius=2)
    bb = im.getchannel("A").getbbox()
    return im.crop(bb) if bb else im


def heal_apples(im):
    """Replace red apple pixels in A3/A4 with surrounding leaf-green shades."""
    out = im.copy()
    px = out.load()
    w, h = out.size
    for _pass in range(4):
        updates = []
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                if a > 0 and is_apple_red(r, g, b):
                    nbrs = []
                    for dy in range(-3, 4):
                        for dx in range(-3, 4):
                            nx, ny = x + dx, y + dy
                            if 0 <= nx < w and 0 <= ny < h:
                                nr, ng, nb, na = px[nx, ny]
                                if na > 0 and is_leaf_green(nr, ng, nb):
                                    nbrs.append((nr, ng, nb, 255))
                    if nbrs:
                        nbrs.sort(key=lambda c: c[1])
                        updates.append((x, y, nbrs[len(nbrs) // 2]))
        for x, y, col in updates:
            px[x, y] = col
    return out


def split_crown_and_bark_base(im, crown_y_limit, base_y_start, raw_im=None):
    """Extract the uncut leafy crown (preserving natural serrated/scalloped bottom leaf tips)
    and a 100% grass-free bark root base."""
    w, h = im.size
    px = im.load()

    crown = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    cpx = crown.load()
    for y in range(min(h, crown_y_limit)):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            is_bark = (not is_leaf_green(r, g, b)) and (r > g - 4) and (g < 145 or abs(r - g) <= 16)
            if y > h * 0.42 and is_bark:
                continue
            cpx[x, y] = (r, g, b, 255)

    # Strip any thin protruding grass-blade tendrils at the bottom of the crown crop
    # (rows in the bottom 25% where a pixel has <= 3 vertical opaque neighbors in its column)
    for _pass in range(2):
        for x in range(w):
            for y in range(min(h, crown_y_limit) - 1, int(crown_y_limit * 0.68), -1):
                if cpx[x, y][3] > 0:
                    up_cnt = sum(1 for dy in range(1, 7) if y - dy >= 0 and cpx[x, y - dy][3] > 0)
                    if up_cnt <= 3:
                        cpx[x, y] = (0, 0, 0, 0)

    crown = keep_largest_component(crown, radius=1)
    cw, ch = crown.size
    cpx = crown.load()

    # Fill tiny 1-2px transparent pinholes inside the crown where bark was masked out
    for _pass in range(2):
        fills = []
        for y in range(2, ch - 2):
            for x in range(2, cw - 2):
                if cpx[x, y][3] == 0:
                    nbrs = []
                    for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1)):
                        nr, ng, nb, na = cpx[x + dx, y + dy]
                        if na > 0:
                            nbrs.append((nr, ng, nb, 255))
                    if len(nbrs) >= 5:
                        nbrs.sort(key=lambda c: c[1])
                        fills.append((x, y, nbrs[len(nbrs) // 2]))
        for x, y, col in fills:
            cpx[x, y] = col

    outline_col = (26, 57, 36, 255)
    shadow_col = (46, 93, 53, 255)
    for x in range(cw):
        for y in range(ch - 1, int(ch * 0.65), -1):
            r, g, b, a = cpx[x, y]
            if a > 0:
                if g > 70:
                    cpx[x, y] = outline_col
                    if y - 1 >= 0 and cpx[x, y - 1][3] > 0 and cpx[x, y - 1][1] > 95:
                        cpx[x, y - 1] = shadow_col
                break

    src_base = raw_im if raw_im is not None else im
    bw, bh_full = src_base.size
    spx = src_base.load()
    bh = bh_full - base_y_start
    base = Image.new("RGBA", (bw, bh), (0, 0, 0, 0))
    bpx = base.load()
    for y in range(base_y_start, bh_full):
        for x in range(bw):
            r, g, b, a = spx[x, y]
            if a == 0:
                continue
            # Strip ALL grass/leaf green pixels from the root base!
            if is_leaf_green(r, g, b) or (g > r and g > b):
                continue
            bpx[x, y - base_y_start] = (r, g, b, 255)
    base = keep_largest_component(base, radius=2)
    return crown, base


FOLIAGE_RAMPS = {
    "oak_emerald": [
        (22, 50, 32), (34, 76, 48), (46, 102, 56),
        (64, 128, 58), (88, 152, 62), (122, 174, 62), (150, 192, 66),
    ],
    "oak_golden": [
        (36, 54, 26), (56, 84, 36), (78, 112, 42),
        (108, 138, 48), (142, 162, 54), (176, 184, 62), (204, 202, 74),
    ],
    "oak_summer": [
        (26, 57, 36), (40, 86, 48), (57, 110, 50),
        (79, 129, 54), (106, 148, 59), (135, 168, 61), (158, 188, 62),
    ],
    "maple_gold_green": [
        (32, 56, 28), (48, 86, 38), (68, 116, 46),
        (98, 144, 52), (138, 168, 56), (178, 186, 60), (210, 204, 70),
    ],
    "maple_amber": [
        (48, 34, 22), (82, 54, 26), (118, 78, 32),
        (156, 106, 38), (192, 136, 46), (222, 166, 56), (242, 194, 74),
    ],
    "maple_crimson": [
        (46, 22, 24), (78, 32, 30), (114, 44, 36),
        (152, 60, 42), (188, 82, 48), (218, 112, 56), (238, 146, 68),
    ],
    "birch_lime": [
        (32, 58, 28), (50, 88, 38), (72, 118, 46),
        (102, 146, 54), (134, 172, 60), (166, 196, 68), (192, 214, 80),
    ],
    "birch_gold": [
        (44, 52, 24), (72, 84, 32), (104, 118, 40),
        (140, 150, 48), (176, 178, 56), (208, 202, 68), (232, 222, 86),
    ],
    "birch_downy": [
        (22, 54, 40), (34, 82, 58), (48, 110, 72),
        (68, 136, 82), (94, 162, 92), (126, 186, 104), (156, 206, 120),
    ],
    "willow_jade": [
        (20, 56, 46), (32, 84, 68), (46, 112, 84),
        (66, 138, 94), (92, 164, 104), (124, 188, 114), (156, 208, 128),
    ],
    "willow_weeping_gold": [
        (26, 58, 40), (42, 88, 58), (62, 118, 70),
        (88, 146, 78), (118, 170, 86), (150, 192, 96), (180, 210, 110),
    ],
    "pine_scots": [
        (18, 46, 38), (26, 68, 54), (36, 92, 70),
        (50, 116, 78), (68, 140, 84), (94, 162, 90), (122, 182, 100),
    ],
    "fir_douglas": [
        (14, 48, 44), (22, 70, 60), (32, 96, 78),
        (46, 122, 88), (64, 148, 96), (86, 170, 104), (114, 190, 116),
    ],
    "fir_silver": [
        (18, 48, 48), (28, 72, 70), (42, 98, 92),
        (60, 124, 114), (84, 150, 136), (114, 176, 160), (148, 202, 186),
    ],
    "savanna_acacia": [
        (42, 58, 24), (66, 90, 32), (92, 120, 42),
        (122, 148, 50), (152, 174, 58), (182, 196, 68), (206, 214, 82),
    ],
    "olive_sage": [
        (34, 50, 34), (52, 76, 50), (72, 102, 66),
        (94, 126, 82), (118, 150, 100), (146, 174, 122), (172, 196, 146),
    ],
}

BARK_RAMPS = {
    "oak_brown": [
        (42, 20, 18), (64, 32, 28), (92, 50, 42),
        (122, 72, 54), (154, 96, 66), (182, 126, 80),
    ],
    "oak_dark": [
        (34, 16, 14), (54, 26, 22), (78, 42, 34),
        (106, 60, 46), (134, 82, 58), (162, 108, 72),
    ],
    "pine_red": [
        (46, 20, 16), (76, 34, 24), (112, 54, 36),
        (148, 78, 48), (182, 106, 64), (210, 136, 82),
    ],
    "birch_white": [
        (48, 42, 40), (88, 82, 78), (142, 138, 130),
        (188, 186, 176), (220, 220, 210), (242, 242, 234),
    ],
    "olive_ash": [
        (46, 40, 38), (72, 64, 58), (102, 94, 82),
        (132, 124, 108), (162, 154, 134), (190, 182, 160),
    ],
}


def remap_foliage_and_bark(im, foliage_ramp=None, bark_ramp=None, lum_shift=0):
    out = im.copy()
    px = out.load()
    w, h = out.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            lum = int(0.299 * r + 0.587 * g + 0.114 * b) + lum_shift
            if is_leaf_green(r, g, b) or (g >= r and g >= b and lum < 68):
                if foliage_ramp is not None:
                    t = (lum - 36) / 136.0
                    idx = max(0, min(6, int(round(t * 6.0))))
                    nr, ng, nb = foliage_ramp[idx]
                    px[x, y] = (nr, ng, nb, 255)
            else:
                if bark_ramp is not None:
                    t = (lum - 32) / 155.0
                    idx = max(0, min(5, int(round(t * 5.0))))
                    nr, ng, nb = bark_ramp[idx]
                    px[x, y] = (nr, ng, nb, 255)
    return out


def paste_crown_lobe(canvas, lobe, dst_x, dst_y, ramp, flip_x=False, lum_shift=0, is_front=True):
    src = lobe.transpose(Image.FLIP_LEFT_RIGHT) if flip_x else lobe
    src = remap_foliage_and_bark(src, foliage_ramp=ramp, lum_shift=lum_shift)
    spx = src.load()
    cpx = canvas.load()
    cw, ch = canvas.size
    sw, sh = src.size
    dst_x = max(1, min(cw - sw - 1, dst_x))
    dst_y = max(1, min(ch - sh - 6, dst_y))

    outline_col = ramp[0]
    crevice_col = ramp[1]
    shadow_col = ramp[2]

    for sy in range(sh):
        dy = dst_y + sy
        if dy < 0 or dy >= ch - 1:
            continue
        for sx in range(sw):
            dx = dst_x + sx
            if dx < 0 or dx >= cw:
                continue
            sr, sg, sb, sa = spx[sx, sy]
            if sa == 0:
                continue
            dr, dg, db, da = cpx[dx, dy]
            is_src_outline = (sr == outline_col[0] and sg == outline_col[1] and sb == outline_col[2])
            if da > 0 and is_src_outline:
                if sy > sh * 0.65 and is_front:
                    cpx[dx, dy] = (crevice_col[0], crevice_col[1], crevice_col[2], 255)
                else:
                    cpx[dx, dy] = (shadow_col[0], shadow_col[1], shadow_col[2], 255)
            else:
                cpx[dx, dy] = (sr, sg, sb, 255)


def draw_organic_bough(px, w, h, x0, y0, x1, y1, r0, r1, bark_ramp, seed):
    steps = max(abs(x1 - x0), abs(y1 - y0), 1) * 2
    for i in range(steps + 1):
        t = i / float(steps)
        curve = math.sin(t * math.pi) * ((hash2d(i, seed, 17) & 3) - 1.5) * 0.5
        cx = x0 + (x1 - x0) * t + curve
        cy = y0 + (y1 - y0) * t
        rad = r0 + (r1 - r0) * t
        ix0 = int(round(cx - rad))
        ix1 = int(round(cx + rad))
        iy0 = int(round(cy - rad * 0.65))
        iy1 = int(round(cy + rad * 0.65))
        for y in range(max(0, iy0), min(h - 1, iy1 + 1)):
            for x in range(max(0, ix0), min(w, ix1 + 1)):
                rel_x = (x - ix0) / max(1.0, float(ix1 - ix0))
                if x == ix0 or x == ix1:
                    col = bark_ramp[0]
                elif rel_x < 0.35:
                    col = bark_ramp[4]
                elif rel_x < 0.68:
                    col = bark_ramp[3]
                else:
                    col = bark_ramp[1]
                px[x, y] = (col[0], col[1], col[2], 255)


def paste_base_anchored(canvas, base_im, cx, bot_y, foliage_ramp, bark_ramp, flip_x=False, widen_px=0):
    """Paste a base sprite anchored at (cx, bot_y). For trees, base_im contains zero grass pixels."""
    src = base_im.transpose(Image.FLIP_LEFT_RIGHT) if flip_x else base_im
    src = remap_foliage_and_bark(src, foliage_ramp=foliage_ramp, bark_ramp=bark_ramp)
    sw, sh = src.size
    cpx = canvas.load()
    spx = src.load()
    cw, ch = canvas.size

    if widen_px > 0:
        for shift in (-widen_px, widen_px):
            for sy in range(sh):
                dy = bot_y - (sh - 1 - sy)
                if dy < 0 or dy >= ch:
                    continue
                for sx in range(sw):
                    dx = cx - sw // 2 + sx + shift
                    if 0 <= dx < cw:
                        r, g, b, a = spx[sx, sy]
                        if a > 0:
                            cpx[dx, dy] = (r, g, b, 255)

    for sy in range(sh):
        dy = bot_y - (sh - 1 - sy)
        if dy < 0 or dy >= ch:
            continue
        for sx in range(sw):
            dx = cx - sw // 2 + sx
            if 0 <= dx < cw:
                r, g, b, a = spx[sx, sy]
                if a > 0:
                    cpx[dx, dy] = (r, g, b, 255)

    anchor_col = (bark_ramp[0][0], bark_ramp[0][1], bark_ramp[0][2], 255)
    for ax in range(max(0, cx - 2), min(cw, cx + 3)):
        if cpx[ax, ch - 1][3] == 0:
            cpx[ax, ch - 1] = anchor_col
        if ch >= 2 and cpx[ax, ch - 2][3] == 0:
            cpx[ax, ch - 2] = (bark_ramp[2][0], bark_ramp[2][1], bark_ramp[2][2], 255)


def draw_willow_curtain_lock(px, w, h, x_top, y_top, length, width_top, sway_dx, ramp, seed):
    """Draw a sculpted pixel-art hanging willow frond lock (tapering leaf curtain)."""
    out_c, crevice_c, shd_c, mid_c, brt_c, hi_c, _ = ramp
    for dy in range(length):
        y = y_top + dy
        if y < 2 or y >= h - 5:
            continue
        t = dy / max(1.0, float(length - 1))
        cur_x = x_top + sway_dx * (t ** 1.3) + math.sin(t * math.pi * 2.0 + seed) * 0.45
        hw = max(0.6, width_top * (1.0 - 0.55 * (t ** 1.4)))
        jag = ((hash2d(dy // 2, int(x_top), seed) & 3) - 1) * 0.35
        x0 = int(round(cur_x - hw + jag))
        x1 = int(round(cur_x + hw + jag))
        for x in range(max(1, x0), min(w - 1, x1 + 1)):
            if dy == length - 1:
                col = out_c
            elif x == x0 or x == x1:
                col = crevice_c if t > 0.35 else shd_c
            else:
                rel = (x - x0) / max(1.0, float(x1 - x0))
                if t < 0.35 and rel < 0.55:
                    col = hi_c if (dy % 3 != 0) else brt_c
                elif t < 0.72:
                    col = brt_c if rel < 0.5 else mid_c
                else:
                    col = mid_c if rel < 0.5 else shd_c
            px[x, y] = (col[0], col[1], col[2], 255)


class MasterParts:
    def __init__(self, sheet):
        a1 = extract_clean(sheet, (336, 0, 400, 74))
        a2 = extract_clean(sheet, (401, 5, 456, 75))
        a3_raw = extract_clean(sheet, (464, 0, 528, 78))
        a4_raw = extract_clean(sheet, (534, 6, 588, 74), [(0, 0, 10, 10)])
        a3 = heal_apples(a3_raw)
        a4 = heal_apples(a4_raw)

        b1 = extract_clean(sheet, (339, 82, 412, 160), [(60, 0, 73, 15), (0, 62, 15, 78)])
        b2 = extract_clean(sheet, (418, 78, 477, 145), [(0, 48, 12, 67), (44, 46, 59, 67)])
        b3 = extract_clean(sheet, (483, 79, 526, 123))
        c1 = extract_clean(sheet, (450, 159, 495, 207))
        c3_raw = extract_clean(sheet, (385, 244, 415, 286))
        c3 = heal_apples(c3_raw)

        # Extract completely grass-free crowns and 100% bark-only root bases
        self.a1_crown, self.a1_base = split_crown_and_bark_base(a1, 61, 48)
        self.a2_crown, self.a2_base = split_crown_and_bark_base(a2, 56, 43)
        self.a3_crown, self.a3_base = split_crown_and_bark_base(a3, 62, 49, raw_im=a3_raw)
        self.a4_crown, self.a4_base = split_crown_and_bark_base(a4, 54, 42, raw_im=a4_raw)
        self.b1_crown, self.b1_base = split_crown_and_bark_base(b1, 64, 46)
        self.b2_crown, self.b2_base = split_crown_and_bark_base(b2, 54, 40)
        self.b3_crown, self.b3_base = split_crown_and_bark_base(b3, 36, 25)
        self.c1_crown, self.c1_base = split_crown_and_bark_base(c1, 38, 28)
        self.c3_crown, self.c3_base = split_crown_and_bark_base(c3, 34, 24, raw_im=c3_raw)

        self.conifer_fir = extract_clean(sheet, (338, 243, 383, 288))
        # Conifer bush lobes with bottom grass skirt trimmed off (y < 22)
        pbd_raw = extract_clean(sheet, (530, 84, 559, 113))
        pbt_raw = extract_clean(sheet, (480, 131, 511, 160))
        self.pine_bush_dark, _ = split_crown_and_bark_base(pbd_raw, 22, 20)
        self.pine_bush_teal, _ = split_crown_and_bark_base(pbt_raw, 23, 20)

        self.carrot_sm = extract_clean(sheet, (161, 321, 174, 336))
        self.carrot_big = extract_clean(sheet, (176, 328, 192, 350))
        self.tomato_sm = extract_clean(sheet, (245, 369, 265, 398))
        self.tomato_med = extract_clean(sheet, (273, 360, 300, 398))
        self.tomato_big = extract_clean(sheet, (305, 360, 332, 398))
        self.flower_stems = extract_clean(sheet, (465, 220, 479, 240), keep_main=False)
        self.reed_tuft = extract_clean(sheet, (367, 150, 385, 181))
        self.spiky_sm = extract_clean(sheet, (337, 289, 368, 319))


def finalize_sprite(canvas):
    w, h = canvas.size
    bb = canvas.getchannel("A").getbbox()
    if not bb:
        return canvas
    x0, y0, x1, y1 = bb
    cx = w // 2
    half_w = max(cx - x0, x1 - cx) + 1
    new_w = half_w * 2
    new_h = h - y0
    out = Image.new("RGBA", (new_w, new_h), (0, 0, 0, 0))
    out.paste(canvas.crop((cx - half_w, y0, cx + half_w, h)), (0, 0))
    opx = out.load()
    ocx = new_w // 2
    if opx[ocx, new_h - 1][3] == 0:
        for sy in range(new_h - 1, -1, -1):
            found = None
            for dx in range(new_w // 2):
                for sx in (ocx - dx, ocx + dx):
                    if 0 <= sx < new_w and opx[sx, sy][3] > 0:
                        found = opx[sx, sy]
                        break
                if found:
                    break
            if found:
                for fill_y in range(sy, new_h):
                    opx[ocx, fill_y] = found
                break
    return out


def build_broadleaf_tree(mp, species, stage, variant):
    big = (stage == "big")

    if species == "quercus_robur":
        # Variant 1: Wide spreading ancient oak (scalloped pillow-lobes b1/b2, deep emerald, wide buttress base)
        # Variant 2: Tall multi-bough golden-summer oak (serrated leaf-spray crowns a3/a1, exposed forked boughs, twin-root base)
        if big:
            if variant == 1:
                ramp = FOLIAGE_RAMPS["oak_emerald"]
                bark = BARK_RAMPS["oak_brown"]
                w, h = 104, 98
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                paste_crown_lobe(im, mp.b1_crown, cx - mp.b1_crown.width // 2, 14, ramp, flip_x=False, lum_shift=-14, is_front=False)
                draw_organic_bough(im.load(), w, h, cx, h - 14, cx - 24, h - 42, 5.2, 2.8, bark, 1)
                draw_organic_bough(im.load(), w, h, cx, h - 14, cx + 24, h - 42, 5.2, 2.8, bark, 8)
                paste_base_anchored(im, mp.a1_base, cx, h - 1, ramp, bark, flip_x=False, widen_px=3)
                lw, lh = mp.b2_crown.size
                paste_crown_lobe(im, mp.b2_crown, cx - lw - 4, h - 16 - lh, ramp, flip_x=False, lum_shift=-5, is_front=True)
                paste_crown_lobe(im, mp.b2_crown, cx + 4, h - 16 - lh, ramp, flip_x=True, lum_shift=-7, is_front=True)
                bw, bh = mp.b1_crown.size
                paste_crown_lobe(im, mp.b1_crown, cx - bw // 2 - 12, 12, ramp, flip_x=False, lum_shift=0, is_front=True)
                paste_crown_lobe(im, mp.b1_crown, cx - bw // 2 + 12, 14, ramp, flip_x=True, lum_shift=-3, is_front=True)
                paste_crown_lobe(im, mp.b1_crown, cx - bw // 2, 2, ramp, flip_x=False, lum_shift=+8, is_front=True)
                return finalize_sprite(im)
            else:
                ramp = FOLIAGE_RAMPS["oak_golden"]
                bark = BARK_RAMPS["oak_dark"]
                w, h = 92, 112
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                paste_crown_lobe(im, mp.a1_crown, cx - mp.a1_crown.width // 2, 18, ramp, flip_x=True, lum_shift=-14, is_front=False)
                draw_organic_bough(im.load(), w, h, cx - 2, h - 14, cx - 20, h - 54, 4.8, 2.5, bark, 2)
                draw_organic_bough(im.load(), w, h, cx + 2, h - 14, cx + 18, h - 58, 4.8, 2.5, bark, 9)
                draw_organic_bough(im.load(), w, h, cx, h - 16, cx + 3, 30, 4.5, 2.2, bark, 15)
                paste_base_anchored(im, mp.b1_base, cx, h - 1, ramp, bark, flip_x=True, widen_px=1)
                paste_crown_lobe(im, mp.a4_crown, cx - mp.a4_crown.width + 4, h - 22 - mp.a4_crown.height, ramp, flip_x=False, lum_shift=-6, is_front=True)
                paste_crown_lobe(im, mp.a2_crown, cx - 2, h - 26 - mp.a2_crown.height, ramp, flip_x=True, lum_shift=-4, is_front=True)
                paste_crown_lobe(im, mp.a3_crown, cx - mp.a3_crown.width // 2 - 8, 18, ramp, flip_x=False, lum_shift=+1, is_front=True)
                paste_crown_lobe(im, mp.a3_crown, cx - mp.a3_crown.width // 2 + 6, 2, ramp, flip_x=True, lum_shift=+8, is_front=True)
                return finalize_sprite(im)
        else:
            if variant == 1:
                ramp = FOLIAGE_RAMPS["oak_emerald"]
                bark = BARK_RAMPS["oak_brown"]
                w, h = 62, 62
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 10, cx, 20, 3.4, 1.8, bark, 1)
                paste_base_anchored(im, mp.a1_base, cx, h - 1, ramp, bark, flip_x=False)
                paste_crown_lobe(im, mp.b3_crown, cx - mp.b3_crown.width // 2 - 7, h - 14 - mp.b3_crown.height, ramp, flip_x=False, lum_shift=-5, is_front=True)
                paste_crown_lobe(im, mp.b3_crown, cx - mp.b3_crown.width // 2 + 7, h - 14 - mp.b3_crown.height, ramp, flip_x=True, lum_shift=-7, is_front=True)
                paste_crown_lobe(im, mp.b2_crown, cx - mp.b2_crown.width // 2, 2, ramp, flip_x=False, lum_shift=+5, is_front=True)
                return finalize_sprite(im)
            else:
                ramp = FOLIAGE_RAMPS["oak_golden"]
                bark = BARK_RAMPS["oak_dark"]
                w, h = 54, 70
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 12, cx - 10, h - 34, 3.2, 1.8, bark, 3)
                draw_organic_bough(im.load(), w, h, cx, h - 12, cx + 10, h - 36, 3.2, 1.8, bark, 7)
                paste_base_anchored(im, mp.a4_base, cx, h - 1, ramp, bark, flip_x=True)
                paste_crown_lobe(im, mp.c1_crown, cx - mp.c1_crown.width // 2 + 5, h - 16 - mp.c1_crown.height, ramp, flip_x=True, lum_shift=-5, is_front=True)
                paste_crown_lobe(im, mp.a4_crown, cx - mp.a4_crown.width // 2 - 2, 2, ramp, flip_x=False, lum_shift=+6, is_front=True)
                return finalize_sprite(im)

    if species == "acer_pseudoplatanus":
        # Variant 1: Sun-lit gold-green symmetric dome maple (serrated leaf-fan crown a1/a2)
        # Variant 2: Fiery autumn amber-crimson asymmetric multi-tiered sycamore maple (lobed b1/a3)
        if big:
            if variant == 1:
                ramp = FOLIAGE_RAMPS["maple_gold_green"]
                bark = BARK_RAMPS["oak_brown"]
                w, h = 94, 102
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                paste_crown_lobe(im, mp.a1_crown, cx - mp.a1_crown.width // 2, 16, ramp, flip_x=False, lum_shift=-12, is_front=False)
                draw_organic_bough(im.load(), w, h, cx, h - 14, cx - 18, h - 42, 4.5, 2.4, bark, 1)
                draw_organic_bough(im.load(), w, h, cx, h - 14, cx + 18, h - 42, 4.5, 2.4, bark, 6)
                paste_base_anchored(im, mp.a1_base, cx, h - 1, ramp, bark, flip_x=False, widen_px=2)
                aw, ah = mp.a2_crown.size
                paste_crown_lobe(im, mp.a2_crown, cx - aw - 1, h - 15 - ah, ramp, flip_x=False, lum_shift=-4, is_front=True)
                paste_crown_lobe(im, mp.a2_crown, cx + 1, h - 15 - ah, ramp, flip_x=True, lum_shift=-7, is_front=True)
                paste_crown_lobe(im, mp.a1_crown, cx - mp.a1_crown.width // 2, 2, ramp, flip_x=False, lum_shift=+8, is_front=True)
                return finalize_sprite(im)
            else:
                ramp = FOLIAGE_RAMPS["maple_crimson"]
                ramp_low = FOLIAGE_RAMPS["maple_amber"]
                bark = BARK_RAMPS["oak_dark"]
                w, h = 104, 110
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 14, cx - 24, h - 48, 4.8, 2.4, bark, 4)
                draw_organic_bough(im.load(), w, h, cx, h - 14, cx + 22, h - 52, 4.8, 2.4, bark, 11)
                paste_base_anchored(im, mp.a3_base, cx, h - 1, ramp, bark, flip_x=True, widen_px=2)
                paste_crown_lobe(im, mp.b2_crown, cx - mp.b2_crown.width - 2, h - 17 - mp.b2_crown.height, ramp_low, flip_x=False, lum_shift=-4, is_front=True)
                paste_crown_lobe(im, mp.a3_crown, cx - 2, h - 19 - mp.a3_crown.height, ramp_low, flip_x=True, lum_shift=-2, is_front=True)
                paste_crown_lobe(im, mp.b1_crown, cx - mp.b1_crown.width // 2 - 8, 16, ramp, flip_x=True, lum_shift=+2, is_front=True)
                paste_crown_lobe(im, mp.a3_crown, cx - mp.a3_crown.width // 2 + 8, 2, ramp, flip_x=False, lum_shift=+8, is_front=True)
                return finalize_sprite(im)
        else:
            if variant == 1:
                ramp = FOLIAGE_RAMPS["maple_gold_green"]
                bark = BARK_RAMPS["oak_brown"]
                w, h = 56, 62
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 10, cx, 20, 3.2, 1.6, bark, 1)
                paste_base_anchored(im, mp.a2_base, cx, h - 1, ramp, bark, flip_x=False)
                paste_crown_lobe(im, mp.c1_crown, cx - mp.c1_crown.width // 2 - 5, h - 13 - mp.c1_crown.height, ramp, flip_x=False, lum_shift=-5, is_front=True)
                paste_crown_lobe(im, mp.c1_crown, cx - mp.c1_crown.width // 2 + 5, h - 13 - mp.c1_crown.height, ramp, flip_x=True, lum_shift=-7, is_front=True)
                paste_crown_lobe(im, mp.a2_crown, cx - mp.a2_crown.width // 2, 2, ramp, flip_x=False, lum_shift=+6, is_front=True)
                return finalize_sprite(im)
            else:
                ramp = FOLIAGE_RAMPS["maple_amber"]
                bark = BARK_RAMPS["oak_dark"]
                w, h = 62, 68
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 12, cx - 12, h - 34, 3.2, 1.8, bark, 9)
                draw_organic_bough(im.load(), w, h, cx, h - 12, cx + 12, h - 34, 3.2, 1.8, bark, 14)
                paste_base_anchored(im, mp.a3_base, cx, h - 1, ramp, bark, flip_x=True)
                paste_crown_lobe(im, mp.b3_crown, cx - mp.b3_crown.width // 2 - 7, h - 15 - mp.b3_crown.height, ramp, flip_x=False, lum_shift=-4, is_front=True)
                paste_crown_lobe(im, mp.b2_crown, cx - mp.b2_crown.width // 2 + 4, 2, FOLIAGE_RAMPS["maple_crimson"], flip_x=True, lum_shift=+6, is_front=True)
                return finalize_sprite(im)

    if species in ("betula_pendula", "betula_pubescens"):
        # Variant 1: Single slender white-bark column with tall conical serrated crown
        # Variant 2: Twin-trunk forked V-shaped white birch grove with airy pillow clusters and golden-lime foliage
        bark = BARK_RAMPS["birch_white"]
        ramp1 = FOLIAGE_RAMPS["birch_lime" if species == "betula_pendula" else "birch_downy"]
        ramp2 = FOLIAGE_RAMPS["birch_gold" if species == "betula_pendula" else "willow_jade"]
        if big:
            if variant == 1:
                w, h = 82, 106
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 14, cx - 3, 24, 4.0, 2.0, bark, 1)
                draw_organic_bough(im.load(), w, h, cx, h - 22, cx - 14, h - 54, 3.2, 1.8, bark, 3)
                draw_organic_bough(im.load(), w, h, cx, h - 24, cx + 14, h - 56, 3.2, 1.8, bark, 5)
                paste_base_anchored(im, mp.b1_base, cx, h - 1, ramp1, bark, flip_x=False)
                paste_crown_lobe(im, mp.c1_crown, cx - mp.c1_crown.width + 2, h - 20 - mp.c1_crown.height, ramp1, flip_x=False, lum_shift=-6, is_front=True)
                paste_crown_lobe(im, mp.c1_crown, cx - 2, h - 22 - mp.c1_crown.height, ramp1, flip_x=True, lum_shift=-8, is_front=True)
                paste_crown_lobe(im, mp.a2_crown, cx - mp.a2_crown.width // 2 - 4, 22, ramp1, flip_x=False, lum_shift=0, is_front=True)
                paste_crown_lobe(im, mp.a4_crown, cx - mp.a4_crown.width // 2 - 2, 2, ramp1, flip_x=False, lum_shift=+7, is_front=True)
                return finalize_sprite(im)
            else:
                w, h = 96, 102
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                # Twin diverging white birch trunks forming an open V-arch
                draw_organic_bough(im.load(), w, h, cx - 3, h - 12, cx - 22, 34, 3.8, 2.0, bark, 2)
                draw_organic_bough(im.load(), w, h, cx + 3, h - 12, cx + 22, 32, 3.8, 2.0, bark, 8)
                paste_base_anchored(im, mp.b2_base, cx, h - 1, ramp2, bark, flip_x=True, widen_px=2)
                paste_crown_lobe(im, mp.b3_crown, cx - mp.b3_crown.width - 4, h - 20 - mp.b3_crown.height, ramp2, flip_x=False, lum_shift=-5, is_front=True)
                paste_crown_lobe(im, mp.b3_crown, cx + 4, h - 18 - mp.b3_crown.height, ramp2, flip_x=True, lum_shift=-7, is_front=True)
                paste_crown_lobe(im, mp.b2_crown, cx - mp.b2_crown.width // 2 - 14, 14, ramp2, flip_x=False, lum_shift=+2, is_front=True)
                paste_crown_lobe(im, mp.b2_crown, cx - mp.b2_crown.width // 2 + 14, 10, ramp2, flip_x=True, lum_shift=+6, is_front=True)
                paste_crown_lobe(im, mp.b3_crown, cx - mp.b3_crown.width // 2, 2, ramp2, flip_x=False, lum_shift=+9, is_front=True)
                return finalize_sprite(im)
        else:
            if variant == 1:
                w, h = 50, 64
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 12, cx - 2, 20, 2.8, 1.5, bark, 1)
                paste_base_anchored(im, mp.b2_base, cx, h - 1, ramp1, bark, flip_x=False)
                paste_crown_lobe(im, mp.c3_crown, cx - mp.c3_crown.width // 2 - 4, h - 16 - mp.c3_crown.height, ramp1, flip_x=False, lum_shift=-5, is_front=True)
                paste_crown_lobe(im, mp.c1_crown, cx - mp.c1_crown.width // 2 + 2, 2, ramp1, flip_x=False, lum_shift=+6, is_front=True)
                return finalize_sprite(im)
            else:
                w, h = 60, 62
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx - 2, h - 10, cx - 12, 24, 2.6, 1.4, bark, 4)
                draw_organic_bough(im.load(), w, h, cx + 2, h - 10, cx + 12, 22, 2.6, 1.4, bark, 9)
                paste_base_anchored(im, mp.b1_base, cx, h - 1, ramp2, bark, flip_x=True)
                paste_crown_lobe(im, mp.b3_crown, cx - mp.b3_crown.width // 2 - 7, 14, ramp2, flip_x=False, lum_shift=-3, is_front=True)
                paste_crown_lobe(im, mp.b3_crown, cx - mp.b3_crown.width // 2 + 7, 4, ramp2, flip_x=True, lum_shift=+6, is_front=True)
                return finalize_sprite(im)

    if species == "salix_babylonica":
        # Variant 1: Wide symmetric weeping dome with jade vertical frond curtains
        # Variant 2: Wind-swept asymmetricleaning river willow with golden-green sweeping locks
        bark = BARK_RAMPS["oak_brown"]
        if big:
            if variant == 1:
                ramp = FOLIAGE_RAMPS["willow_jade"]
                w, h = 98, 98
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                px = im.load()
                for dx in range(-34, 35, 6):
                    draw_willow_curtain_lock(px, w, h, cx + dx, 38 + abs(dx) // 4, 36 - abs(dx) // 4, 2.4, (dx / 22.0), ramp, 1 + dx)
                draw_organic_bough(px, w, h, cx, h - 14, cx - 22, 34, 4.8, 2.5, bark, 1)
                draw_organic_bough(px, w, h, cx, h - 14, cx + 22, 34, 4.8, 2.5, bark, 5)
                paste_base_anchored(im, mp.a1_base, cx, h - 1, ramp, bark, flip_x=False, widen_px=2)
                paste_crown_lobe(im, mp.b2_crown, cx - mp.b2_crown.width - 1, 16, ramp, flip_x=False, lum_shift=-4, is_front=True)
                paste_crown_lobe(im, mp.b2_crown, cx + 1, 16, ramp, flip_x=True, lum_shift=-6, is_front=True)
                paste_crown_lobe(im, mp.b1_crown, cx - mp.b1_crown.width // 2, 2, ramp, flip_x=False, lum_shift=+6, is_front=True)
                px = im.load()
                for idx_l, dx in enumerate([-36, -31, -26, -21, -15, -10, 10, 15, 21, 26, 31, 36]):
                    start_y = 32 + (abs(dx) // 5) - (idx_l % 2) * 4
                    lock_len = 36 + ((hash2d(dx, 1, 41) % 8) - 3) - max(0, (18 - abs(dx)))
                    draw_willow_curtain_lock(px, w, h, cx + dx, start_y, lock_len, 2.3, -1.2 if dx < 0 else 1.2, ramp, 17 + idx_l)
                return finalize_sprite(im)
            else:
                ramp = FOLIAGE_RAMPS["willow_weeping_gold"]
                w, h = 104, 106
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                px = im.load()
                # Leaning trunk curving right with cascading wind-swept locks
                draw_organic_bough(px, w, h, cx - 4, h - 14, cx - 26, 42, 4.5, 2.4, bark, 2)
                draw_organic_bough(px, w, h, cx - 2, h - 14, cx + 18, 26, 5.0, 2.6, bark, 9)
                paste_base_anchored(im, mp.b1_base, cx - 2, h - 1, ramp, bark, flip_x=True, widen_px=2)
                paste_crown_lobe(im, mp.a2_crown, cx - mp.a2_crown.width - 4, 24, ramp, flip_x=False, lum_shift=-5, is_front=True)
                paste_crown_lobe(im, mp.a1_crown, cx - 10, 14, ramp, flip_x=True, lum_shift=-2, is_front=True)
                paste_crown_lobe(im, mp.a3_crown, cx - mp.a3_crown.width // 2 + 8, 2, ramp, flip_x=False, lum_shift=+7, is_front=True)
                px = im.load()
                for idx_l, dx in enumerate([-38, -32, -25, -18, -12, 6, 13, 19, 25, 31, 37, 42]):
                    start_y = (38 if dx < 0 else 28) + (abs(dx) // 6)
                    lock_len = (34 if dx < 0 else 44) - max(0, 16 - abs(dx))
                    draw_willow_curtain_lock(px, w, h, cx + dx, start_y, lock_len, 2.4, 3.2, ramp, 37 + idx_l)
                return finalize_sprite(im)
        else:
            if variant == 1:
                ramp = FOLIAGE_RAMPS["willow_jade"]
                w, h = 58, 62
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 10, cx, 18, 3.2, 1.6, bark, 1)
                paste_base_anchored(im, mp.a2_base, cx, h - 1, ramp, bark, flip_x=False)
                paste_crown_lobe(im, mp.b3_crown, cx - mp.b3_crown.width // 2 - 6, 12, ramp, flip_x=False, lum_shift=-3, is_front=True)
                paste_crown_lobe(im, mp.b3_crown, cx - mp.b3_crown.width // 2 + 6, 12, ramp, flip_x=True, lum_shift=-5, is_front=True)
                paste_crown_lobe(im, mp.b2_crown, cx - mp.b2_crown.width // 2, 2, ramp, flip_x=False, lum_shift=+5, is_front=True)
                px = im.load()
                for idx_l, dx in enumerate([-20, -15, -10, -6, 6, 10, 15, 20]):
                    draw_willow_curtain_lock(px, w, h, cx + dx, 22 + abs(dx) // 5, 22 - max(0, 10 - abs(dx)), 1.8, -0.8 if dx < 0 else 0.8, ramp, 13 + idx_l)
                return finalize_sprite(im)
            else:
                ramp = FOLIAGE_RAMPS["willow_weeping_gold"]
                w, h = 64, 66
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx - 2, h - 12, cx + 10, 22, 3.2, 1.8, bark, 7)
                paste_base_anchored(im, mp.b2_base, cx - 1, h - 1, ramp, bark, flip_x=True)
                paste_crown_lobe(im, mp.c1_crown, cx - mp.c1_crown.width // 2 - 8, 16, ramp, flip_x=False, lum_shift=-4, is_front=True)
                paste_crown_lobe(im, mp.a4_crown, cx - mp.a4_crown.width // 2 + 5, 2, ramp, flip_x=True, lum_shift=+6, is_front=True)
                px = im.load()
                for idx_l, dx in enumerate([-22, -16, -10, 4, 10, 16, 22]):
                    draw_willow_curtain_lock(px, w, h, cx + dx, (26 if dx < 0 else 20) + abs(dx) // 6, 24, 1.9, 2.2, ramp, 29 + idx_l)
                return finalize_sprite(im)

    if species == "acacia_senegal":
        # Variant 1: Wide single-umbrella flat-topped savanna acacia
        # Variant 2: Multi-tiered stepped pagoda savanna acacia with twin offset canopies
        bark = BARK_RAMPS["oak_brown"]
        if big:
            if variant == 1:
                ramp = FOLIAGE_RAMPS["savanna_acacia"]
                w, h = 104, 84
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 14, cx - 30, 28, 3.8, 1.8, bark, 1)
                draw_organic_bough(im.load(), w, h, cx, h - 14, cx + 30, 26, 3.8, 1.8, bark, 4)
                draw_organic_bough(im.load(), w, h, cx, h - 18, cx - 10, 20, 3.4, 1.8, bark, 7)
                draw_organic_bough(im.load(), w, h, cx, h - 18, cx + 12, 20, 3.4, 1.8, bark, 10)
                paste_base_anchored(im, mp.a1_base, cx, h - 1, ramp, bark, flip_x=False, widen_px=1)
                pad_lg = mp.a1_crown.resize((int(mp.a1_crown.width * 1.18), int(mp.a1_crown.height * 0.54)), Image.NEAREST)
                pad_md = mp.a2_crown.resize((int(mp.a2_crown.width * 1.14), int(mp.a2_crown.height * 0.52)), Image.NEAREST)
                paste_crown_lobe(im, pad_md, cx - pad_md.width - 5, 20, ramp, flip_x=False, lum_shift=-4, is_front=True)
                paste_crown_lobe(im, pad_md, cx + 5, 18, ramp, flip_x=True, lum_shift=-6, is_front=True)
                paste_crown_lobe(im, pad_lg, cx - pad_lg.width // 2, 2, ramp, flip_x=False, lum_shift=+6, is_front=True)
                return finalize_sprite(im)
            else:
                ramp = FOLIAGE_RAMPS["oak_golden"]
                w, h = 98, 94
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                # Stepped two-tier savanna limbs
                draw_organic_bough(im.load(), w, h, cx, h - 14, cx - 26, 46, 3.8, 1.8, bark, 2)
                draw_organic_bough(im.load(), w, h, cx, h - 14, cx + 22, 24, 4.0, 2.0, bark, 8)
                draw_organic_bough(im.load(), w, h, cx + 8, 42, cx - 8, 18, 2.8, 1.6, bark, 13)
                paste_base_anchored(im, mp.a3_base, cx, h - 1, ramp, bark, flip_x=True, widen_px=1)
                pad_low = mp.b2_crown.resize((int(mp.b2_crown.width * 1.12), int(mp.b2_crown.height * 0.52)), Image.NEAREST)
                pad_top = mp.b1_crown.resize((int(mp.b1_crown.width * 1.08), int(mp.b1_crown.height * 0.52)), Image.NEAREST)
                paste_crown_lobe(im, pad_low, cx - pad_low.width - 2, 34, ramp, flip_x=False, lum_shift=-5, is_front=True)
                paste_crown_lobe(im, pad_low, cx + 4, 24, ramp, flip_x=True, lum_shift=-2, is_front=True)
                paste_crown_lobe(im, pad_top, cx - pad_top.width // 2 + 4, 2, ramp, flip_x=True, lum_shift=+7, is_front=True)
                return finalize_sprite(im)
        else:
            if variant == 1:
                ramp = FOLIAGE_RAMPS["savanna_acacia"]
                w, h = 62, 54
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 12, cx - 15, 20, 2.8, 1.4, bark, 1)
                draw_organic_bough(im.load(), w, h, cx, h - 12, cx + 15, 20, 2.8, 1.4, bark, 3)
                paste_base_anchored(im, mp.a2_base, cx, h - 1, ramp, bark, flip_x=False)
                pad_sm = mp.c1_crown.resize((int(mp.c1_crown.width * 1.15), int(mp.c1_crown.height * 0.56)), Image.NEAREST)
                paste_crown_lobe(im, pad_sm, cx - pad_sm.width // 2 - 9, 12, ramp, flip_x=False, lum_shift=-4, is_front=True)
                paste_crown_lobe(im, pad_sm, cx - pad_sm.width // 2 + 9, 12, ramp, flip_x=True, lum_shift=-6, is_front=True)
                paste_crown_lobe(im, pad_sm, cx - pad_sm.width // 2, 2, ramp, flip_x=False, lum_shift=+5, is_front=True)
                return finalize_sprite(im)
            else:
                ramp = FOLIAGE_RAMPS["oak_golden"]
                w, h = 58, 62
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 12, cx - 14, 30, 2.8, 1.4, bark, 5)
                draw_organic_bough(im.load(), w, h, cx, h - 12, cx + 12, 16, 2.8, 1.4, bark, 9)
                paste_base_anchored(im, mp.a4_base, cx, h - 1, ramp, bark, flip_x=True)
                pad_sm = mp.b3_crown.resize((int(mp.b3_crown.width * 1.12), int(mp.b3_crown.height * 0.58)), Image.NEAREST)
                paste_crown_lobe(im, pad_sm, cx - pad_sm.width // 2 - 8, 20, ramp, flip_x=False, lum_shift=-5, is_front=True)
                paste_crown_lobe(im, pad_sm, cx - pad_sm.width // 2 + 6, 2, ramp, flip_x=True, lum_shift=+6, is_front=True)
                return finalize_sprite(im)

    # olea_europaea (Mediterranean Olive)
    # Variant 1: Gnarled wide split-trunk ancient olive with silvery-sage pillow crown
    # Variant 2: Twisted asymmetric olive tree with serrated sunlit olive-gold crown
    bark = BARK_RAMPS["olive_ash"]
    if big:
        if variant == 1:
            ramp = FOLIAGE_RAMPS["olive_sage"]
            w, h = 92, 90
            im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            cx = w // 2
            draw_organic_bough(im.load(), w, h, cx - 3, h - 14, cx - 22, 36, 4.8, 2.5, bark, 1)
            draw_organic_bough(im.load(), w, h, cx + 3, h - 14, cx + 22, 34, 4.8, 2.5, bark, 6)
            paste_base_anchored(im, mp.b1_base, cx, h - 1, ramp, bark, flip_x=False, widen_px=2)
            paste_crown_lobe(im, mp.b2_crown, cx - mp.b2_crown.width - 1, h - 18 - mp.b2_crown.height, ramp, flip_x=False, lum_shift=-5, is_front=True)
            paste_crown_lobe(im, mp.b2_crown, cx + 1, h - 18 - mp.b2_crown.height, ramp, flip_x=True, lum_shift=-7, is_front=True)
            paste_crown_lobe(im, mp.b1_crown, cx - mp.b1_crown.width // 2, 2, ramp, flip_x=False, lum_shift=+6, is_front=True)
            return finalize_sprite(im)
        else:
            ramp = FOLIAGE_RAMPS["birch_downy"]
            w, h = 86, 98
            im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            cx = w // 2
            draw_organic_bough(im.load(), w, h, cx + 2, h - 14, cx - 20, 42, 4.6, 2.2, bark, 3)
            draw_organic_bough(im.load(), w, h, cx - 2, h - 14, cx + 18, 26, 4.6, 2.2, bark, 9)
            paste_base_anchored(im, mp.b2_base, cx, h - 1, ramp, bark, flip_x=True, widen_px=2)
            paste_crown_lobe(im, mp.a4_crown, cx - mp.a4_crown.width + 2, h - 18 - mp.a4_crown.height, ramp, flip_x=False, lum_shift=-5, is_front=True)
            paste_crown_lobe(im, mp.a2_crown, cx - 4, h - 22 - mp.a2_crown.height, ramp, flip_x=True, lum_shift=-2, is_front=True)
            paste_crown_lobe(im, mp.a3_crown, cx - mp.a3_crown.width // 2 + 5, 2, ramp, flip_x=True, lum_shift=+7, is_front=True)
            return finalize_sprite(im)
    else:
        if variant == 1:
            ramp = FOLIAGE_RAMPS["olive_sage"]
            w, h = 56, 56
            im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            cx = w // 2
            draw_organic_bough(im.load(), w, h, cx, h - 10, cx - 6, 22, 3.2, 1.6, bark, 1)
            draw_organic_bough(im.load(), w, h, cx, h - 10, cx + 6, 22, 3.2, 1.6, bark, 5)
            paste_base_anchored(im, mp.b2_base, cx, h - 1, ramp, bark, flip_x=False)
            paste_crown_lobe(im, mp.b3_crown, cx - mp.b3_crown.width // 2 - 6, h - 14 - mp.b3_crown.height, ramp, flip_x=False, lum_shift=-4, is_front=True)
            paste_crown_lobe(im, mp.b3_crown, cx - mp.b3_crown.width // 2 + 6, h - 14 - mp.b3_crown.height, ramp, flip_x=True, lum_shift=-6, is_front=True)
            paste_crown_lobe(im, mp.b2_crown, cx - mp.b2_crown.width // 2, 2, ramp, flip_x=False, lum_shift=+5, is_front=True)
            return finalize_sprite(im)
        else:
            ramp = FOLIAGE_RAMPS["birch_downy"]
            w, h = 52, 62
            im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            cx = w // 2
            draw_organic_bough(im.load(), w, h, cx, h - 12, cx + 8, 22, 3.0, 1.6, bark, 5)
            paste_base_anchored(im, mp.b1_base, cx, h - 1, ramp, bark, flip_x=True)
            paste_crown_lobe(im, mp.c1_crown, cx - mp.c1_crown.width // 2 - 5, h - 15 - mp.c1_crown.height, ramp, flip_x=False, lum_shift=-4, is_front=True)
            paste_crown_lobe(im, mp.a4_crown, cx - mp.a4_crown.width // 2 + 4, 2, ramp, flip_x=True, lum_shift=+6, is_front=True)
            return finalize_sprite(im)


def build_conifer_tree(mp, species, stage, variant):
    big = (stage == "big")

    if species == "pinus_sylvestris":
        # Variant 1: Classic Scots Pine with high umbrella needle crown and exposed red-bark upper trunk
        # Variant 2: Wind-swept asymmetric crag pine with multi-level needle pads and forked red trunk
        bark = BARK_RAMPS["pine_red"]
        if big:
            if variant == 1:
                ramp = FOLIAGE_RAMPS["pine_scots"]
                w, h = 88, 102
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 14, cx - 2, 28, 4.4, 2.4, bark, 1)
                draw_organic_bough(im.load(), w, h, cx, h - 24, cx - 20, h - 46, 3.6, 2.0, bark, 3)
                draw_organic_bough(im.load(), w, h, cx, h - 28, cx + 20, h - 50, 3.6, 2.0, bark, 6)
                paste_base_anchored(im, mp.a1_base, cx, h - 1, ramp, bark, flip_x=False, widen_px=1)
                paste_crown_lobe(im, mp.b2_crown, cx - mp.b2_crown.width - 1, h - 20 - mp.b2_crown.height, ramp, flip_x=False, lum_shift=-6, is_front=True)
                paste_crown_lobe(im, mp.b2_crown, cx + 1, h - 22 - mp.b2_crown.height, ramp, flip_x=True, lum_shift=-8, is_front=True)
                paste_crown_lobe(im, mp.conifer_fir, cx - mp.conifer_fir.width // 2 - 12, 22, ramp, flip_x=False, lum_shift=0, is_front=True)
                paste_crown_lobe(im, mp.conifer_fir, cx - mp.conifer_fir.width // 2 + 12, 24, ramp, flip_x=True, lum_shift=-3, is_front=True)
                paste_crown_lobe(im, mp.b1_crown, cx - mp.b1_crown.width // 2, 2, ramp, flip_x=False, lum_shift=+6, is_front=True)
                return finalize_sprite(im)
            else:
                ramp = FOLIAGE_RAMPS["fir_douglas"]
                w, h = 94, 108
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                # Forked leaning crag pine with exposed red-orange branches between needle tiers
                draw_organic_bough(im.load(), w, h, cx - 2, h - 14, cx + 8, 22, 4.6, 2.2, bark, 2)
                draw_organic_bough(im.load(), w, h, cx - 2, h - 20, cx - 24, 48, 3.8, 1.8, bark, 7)
                draw_organic_bough(im.load(), w, h, cx + 4, 54, cx + 26, 36, 3.4, 1.8, bark, 11)
                paste_base_anchored(im, mp.a3_base, cx - 2, h - 1, ramp, bark, flip_x=True, widen_px=1)
                paste_crown_lobe(im, mp.conifer_fir, cx - mp.conifer_fir.width - 4, 34, ramp, flip_x=False, lum_shift=-5, is_front=True)
                paste_crown_lobe(im, mp.conifer_fir, cx + 2, 24, ramp, flip_x=True, lum_shift=-2, is_front=True)
                paste_crown_lobe(im, mp.a3_crown, cx - mp.a3_crown.width // 2 + 6, 2, ramp, flip_x=True, lum_shift=+7, is_front=True)
                return finalize_sprite(im)
        else:
            if variant == 1:
                ramp = FOLIAGE_RAMPS["pine_scots"]
                w, h = 52, 60
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 10, cx, 18, 3.2, 1.6, bark, 1)
                paste_base_anchored(im, mp.a2_base, cx, h - 1, ramp, bark, flip_x=False)
                paste_crown_lobe(im, mp.b3_crown, cx - mp.b3_crown.width // 2 - 5, h - 15 - mp.b3_crown.height, ramp, flip_x=False, lum_shift=-5, is_front=True)
                paste_crown_lobe(im, mp.b3_crown, cx - mp.b3_crown.width // 2 + 5, h - 15 - mp.b3_crown.height, ramp, flip_x=True, lum_shift=-7, is_front=True)
                paste_crown_lobe(im, mp.conifer_fir, cx - mp.conifer_fir.width // 2, 2, ramp, flip_x=False, lum_shift=+5, is_front=True)
                return finalize_sprite(im)
            else:
                ramp = FOLIAGE_RAMPS["fir_douglas"]
                w, h = 58, 66
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 12, cx - 12, 30, 3.0, 1.5, bark, 4)
                draw_organic_bough(im.load(), w, h, cx, h - 12, cx + 8, 18, 3.2, 1.6, bark, 9)
                paste_base_anchored(im, mp.a4_base, cx, h - 1, ramp, bark, flip_x=True)
                paste_crown_lobe(im, mp.pine_bush_dark, cx - mp.pine_bush_dark.width - 1, 24, ramp, flip_x=False, lum_shift=-4, is_front=True)
                paste_crown_lobe(im, mp.conifer_fir, cx - mp.conifer_fir.width // 2 + 5, 2, ramp, flip_x=True, lum_shift=+6, is_front=True)
                return finalize_sprite(im)

    # pseudotsuga_menziesii (Douglas Fir) & abies_alba (Silver Fir)
    # Variant 1: Tall single-spire pyramidal alpine conifer with 4-tier needle skirts
    # Variant 2: Twin-spire uneven forest conifer (main tall spire + secondary side leader spire)
    if species == "pseudotsuga_menziesii":
        ramp1 = FOLIAGE_RAMPS["fir_douglas"]
        ramp2 = FOLIAGE_RAMPS["oak_emerald"]
        bark = BARK_RAMPS["oak_brown"]
    else:
        ramp1 = FOLIAGE_RAMPS["fir_silver"]
        ramp2 = FOLIAGE_RAMPS["birch_downy"]
        bark = BARK_RAMPS["olive_ash"]

    cf = mp.conifer_fir
    cw, ch = cf.size
    if big:
        if variant == 1:
            w, h = 82, 108
            im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            cx = w // 2
            paste_base_anchored(im, mp.a1_base if species == "pseudotsuga_menziesii" else mp.b1_base,
                                cx, h - 1, ramp1, bark, flip_x=False, widen_px=1)
            y_bot = h - 14 - ch
            paste_crown_lobe(im, cf, cx - cw // 2 - 14, y_bot, ramp1, flip_x=False, lum_shift=-8, is_front=True)
            paste_crown_lobe(im, cf, cx - cw // 2 + 14, y_bot, ramp1, flip_x=True, lum_shift=-10, is_front=True)
            paste_crown_lobe(im, cf, cx - cw // 2, y_bot + 2, ramp1, flip_x=False, lum_shift=-4, is_front=True)
            y_mid1 = y_bot - 20
            paste_crown_lobe(im, cf, cx - cw // 2 - 9, y_mid1, ramp1, flip_x=True, lum_shift=-3, is_front=True)
            paste_crown_lobe(im, cf, cx - cw // 2 + 9, y_mid1, ramp1, flip_x=False, lum_shift=-5, is_front=True)
            paste_crown_lobe(im, cf, cx - cw // 2, y_mid1 + 2, ramp1, flip_x=True, lum_shift=+1, is_front=True)
            y_mid2 = y_mid1 - 18
            paste_crown_lobe(im, cf, cx - cw // 2 - 4, y_mid2, ramp1, flip_x=False, lum_shift=+2, is_front=True)
            paste_crown_lobe(im, cf, cx - cw // 2 + 4, y_mid2, ramp1, flip_x=True, lum_shift=0, is_front=True)
            paste_crown_lobe(im, cf, cx - cw // 2, 2, ramp1, flip_x=False, lum_shift=+8, is_front=True)
            return finalize_sprite(im)
        else:
            # Twin-spire conifer: tall main spire on left/center + secondary shorter spire on right
            w, h = 96, 114
            im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            cx = w // 2
            draw_organic_bough(im.load(), w, h, cx - 3, h - 12, cx - 10, 30, 4.2, 2.0, bark, 2)
            draw_organic_bough(im.load(), w, h, cx + 3, h - 12, cx + 16, 48, 3.8, 1.8, bark, 8)
            paste_base_anchored(im, mp.a3_base if species == "pseudotsuga_menziesii" else mp.b2_base,
                                cx, h - 1, ramp2, bark, flip_x=True, widen_px=2)
            y_bot = h - 14 - ch
            # Left tall spire
            paste_crown_lobe(im, cf, cx - cw // 2 - 18, y_bot, ramp2, flip_x=False, lum_shift=-7, is_front=True)
            paste_crown_lobe(im, cf, cx - cw // 2 - 12, y_bot - 22, ramp2, flip_x=True, lum_shift=-2, is_front=True)
            paste_crown_lobe(im, cf, cx - cw // 2 - 8, y_bot - 42, ramp2, flip_x=False, lum_shift=+3, is_front=True)
            paste_crown_lobe(im, cf, cx - cw // 2 - 6, 2, ramp2, flip_x=True, lum_shift=+8, is_front=True)
            # Right secondary spire
            paste_crown_lobe(im, cf, cx - cw // 2 + 18, y_bot + 2, ramp2, flip_x=True, lum_shift=-9, is_front=True)
            paste_crown_lobe(im, cf, cx - cw // 2 + 16, y_bot - 16, ramp2, flip_x=False, lum_shift=-4, is_front=True)
            paste_crown_lobe(im, mp.pine_bush_teal, cx + 4, 24, ramp2, flip_x=True, lum_shift=+4, is_front=True)
            return finalize_sprite(im)
    else:
        if variant == 1:
            w, h = 50, 64
            im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            cx = w // 2
            draw_organic_bough(im.load(), w, h, cx, h - 10, cx, 18, 3.0, 1.5, bark, 1)
            paste_base_anchored(im, mp.a2_base, cx, h - 1, ramp1, bark, flip_x=False)
            y_bot = h - 12 - ch
            paste_crown_lobe(im, cf, cx - cw // 2 - 5, y_bot, ramp1, flip_x=False, lum_shift=-5, is_front=True)
            paste_crown_lobe(im, cf, cx - cw // 2 + 5, y_bot, ramp1, flip_x=True, lum_shift=-7, is_front=True)
            paste_crown_lobe(im, cf, cx - cw // 2, 2, ramp1, flip_x=False, lum_shift=+6, is_front=True)
            return finalize_sprite(im)
        else:
            w, h = 58, 68
            im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            cx = w // 2
            draw_organic_bough(im.load(), w, h, cx, h - 10, cx - 5, 18, 3.0, 1.5, bark, 4)
            draw_organic_bough(im.load(), w, h, cx, h - 10, cx + 8, 26, 2.8, 1.4, bark, 9)
            paste_base_anchored(im, mp.b2_base, cx, h - 1, ramp2, bark, flip_x=True)
            y_bot = h - 12 - ch
            paste_crown_lobe(im, cf, cx - cw // 2 - 8, y_bot, ramp2, flip_x=False, lum_shift=-4, is_front=True)
            paste_crown_lobe(im, mp.pine_bush_teal, cx + 1, y_bot + 8, ramp2, flip_x=True, lum_shift=-6, is_front=True)
            paste_crown_lobe(im, cf, cx - cw // 2 - 5, 2, ramp2, flip_x=True, lum_shift=+6, is_front=True)
            return finalize_sprite(im)


def build_cactus(mp, stage, variant):
    big = (stage == "big")
    ramp = FOLIAGE_RAMPS["oak_emerald" if variant == 1 else "fir_douglas"]
    bark = BARK_RAMPS["oak_brown"]
    w, h = ((48, 64) if variant == 1 else (54, 60)) if big else ((32, 42) if variant == 1 else (38, 40))
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    cx = w // 2
    paste_base_anchored(im, mp.a2_base if big else mp.b3_base, cx, h - 1, ramp, bark, flip_x=(variant == 2))
    px = im.load()
    out_c, shd_c, low_c, mid_c, brt_c, hi_c, _ = ramp

    top_y = 6 if big else 8
    rx = 5 if big else 3
    for y in range(top_y, h - 4):
        r = rx if y > top_y + 2 else max(2, rx - (top_y + 2 - y))
        for x in range(cx - r, cx + r + 1):
            dx = x - cx
            if abs(dx) == r or y == top_y:
                col = out_c
            elif dx == -r + 1 or (dx < 0 and dx % 2 == 0):
                col = hi_c if y < h // 2 else brt_c
            elif dx == 0:
                col = low_c
            elif dx > 0 and dx % 2 == 0:
                col = mid_c
            else:
                col = shd_c
            px[x, y] = (col[0], col[1], col[2], 255)

    if big:
        arm_configs = (
            [(-1, int(h * 0.58), int(h * 0.24)), (1, int(h * 0.46), int(h * 0.18))]
            if variant == 1 else
            [(-1, int(h * 0.44), int(h * 0.16)), (1, int(h * 0.62), int(h * 0.28)), (-1, int(h * 0.72), int(h * 0.48))]
        )
    else:
        arm_configs = (
            [(-1, int(h * 0.60), int(h * 0.34))]
            if variant == 1 else
            [(-1, int(h * 0.56), int(h * 0.32)), (1, int(h * 0.66), int(h * 0.42))]
        )

    ar_w = 3 if big else 2
    for adir, ay_base, ay_top in arm_configs:
        ax = cx + adir * (rx + (7 if big else 5))
        for x in range(min(cx, ax), max(cx, ax) + 1):
            for dy in range(-ar_w, ar_w + 1):
                yy = ay_base + dy
                if 0 <= yy < h - 4 and 0 <= x < w:
                    col = out_c if abs(dy) == ar_w else (brt_c if dy < 0 else shd_c)
                    px[x, yy] = (col[0], col[1], col[2], 255)
        for y in range(ay_top, ay_base + ar_w):
            for dx in range(-ar_w, ar_w + 1):
                xx = ax + dx
                if 0 <= xx < w and 0 <= y < h - 4:
                    col = out_c if (abs(dx) == ar_w or y == ay_top) else (hi_c if dx < 0 else (mid_c if dx == 0 else shd_c))
                    px[xx, y] = (col[0], col[1], col[2], 255)
        fl_cols = (238, 102, 148) if variant == 1 else (255, 196, 64)
        for fdx in (-1, 0, 1):
            if 0 <= ax + fdx < w and ay_top - 2 >= 0:
                px[ax + fdx, ay_top - 1] = (fl_cols[0], fl_cols[1], fl_cols[2], 255)
                if fdx == 0:
                    px[ax, ay_top - 2] = (255, 240, 150, 255)
    return finalize_sprite(im)


def build_herb_or_crop(mp, species, stage, variant):
    big = (stage == "big")
    ramp = FOLIAGE_RAMPS["oak_summer" if variant == 1 else "oak_emerald"]
    bark = BARK_RAMPS["oak_brown"]

    if species == "daucus_carota":
        w, h = ((38, 36) if big else (26, 24))
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        cx = w // 2
        if big:
            if variant == 1:
                offsets = [(-6, -2, mp.carrot_big, False), (6, -1, mp.carrot_big, True), (0, 0, mp.carrot_big, False)]
            else:
                offsets = [(-10, -4, mp.carrot_sm, True), (10, -4, mp.carrot_sm, False), (-4, -1, mp.carrot_big, True), (4, 0, mp.carrot_big, False)]
        else:
            if variant == 1:
                offsets = [(0, 0, mp.carrot_sm, False)]
            else:
                offsets = [(-4, -2, mp.carrot_sm, True), (3, 0, mp.carrot_big, False)]
        for ox, oy, spr, fl in offsets:
            s = spr.transpose(Image.FLIP_LEFT_RIGHT) if fl else spr
            im.alpha_composite(s, (cx + ox - s.width // 2, h - s.height + oy))
        paste_base_anchored(im, offsets[-1][2], cx, h - 1, None, bark)
        return finalize_sprite(im)

    if species == "solanum_lycopersicum":
        w, h = ((46, 46) if big else (32, 36))
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        cx = w // 2
        if big:
            if variant == 1:
                im.alpha_composite(mp.tomato_med.transpose(Image.FLIP_LEFT_RIGHT), (cx - mp.tomato_med.width // 2 - 6, h - mp.tomato_med.height - 2))
                im.alpha_composite(mp.tomato_big, (cx - mp.tomato_big.width // 2 + 4, h - mp.tomato_big.height))
            else:
                im.alpha_composite(mp.tomato_big.transpose(Image.FLIP_LEFT_RIGHT), (cx - mp.tomato_big.width // 2 - 7, h - mp.tomato_big.height - 1))
                im.alpha_composite(mp.tomato_big, (cx - mp.tomato_big.width // 2 + 7, h - mp.tomato_big.height - 1))
                im.alpha_composite(mp.tomato_med, (cx - mp.tomato_med.width // 2, h - mp.tomato_med.height))
        else:
            if variant == 1:
                im.alpha_composite(mp.tomato_sm, (cx - mp.tomato_sm.width // 2, h - mp.tomato_sm.height))
            else:
                im.alpha_composite(mp.tomato_sm.transpose(Image.FLIP_LEFT_RIGHT), (cx - mp.tomato_sm.width // 2 - 4, h - mp.tomato_sm.height - 1))
                im.alpha_composite(mp.tomato_med, (cx - mp.tomato_med.width // 2 + 3, h - mp.tomato_med.height))
        return finalize_sprite(im)

    if species in ("linum_usitatissimum", "bellis_perennis"):
        w, h = ((42, 40) if big else (28, 28))
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        cx = w // 2
        base_tuft = mp.reed_tuft if species == "linum_usitatissimum" else mp.spiky_sm
        paste_base_anchored(im, base_tuft, cx, h - 1, ramp, bark, flip_x=(variant == 2), widen_px=(4 if big else 0))
        if big:
            im.alpha_composite(mp.flower_stems, (cx - mp.flower_stems.width - 2, h - mp.flower_stems.height - 4))
            im.alpha_composite(mp.flower_stems.transpose(Image.FLIP_LEFT_RIGHT), (cx + 2, h - mp.flower_stems.height - 4))
        im.alpha_composite(mp.flower_stems, (cx - mp.flower_stems.width // 2, h - mp.flower_stems.height - (6 if big else 2)))

        px = im.load()
        if big:
            blossoms = [(-9, 10), (-4, 7), (1, 6), (6, 8), (10, 11), (-2, 13), (4, 14)] if variant == 1 else [(-11, 8), (-6, 11), (-1, 5), (5, 7), (9, 10), (-5, 15), (3, 13), (8, 15)]
        else:
            blossoms = [(-4, 7), (1, 5), (5, 8)] if variant == 1 else [(-6, 6), (-1, 9), (4, 5), (6, 10)]
        for bx_off, by in blossoms:
            bx = cx + bx_off
            if 2 <= bx < w - 2 and 2 <= by < h - 4:
                if species == "linum_usitatissimum":
                    petal = (96, 168, 248, 255) if variant == 1 else (138, 198, 255, 255)
                    center = (250, 232, 110, 255)
                else:
                    petal = (248, 250, 252, 255) if variant == 1 else (255, 224, 238, 255)
                    center = (248, 198, 54, 255)
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1)):
                    px[bx + dx, by + dy] = petal
                px[bx, by] = center
        return finalize_sprite(im)

    # typha_latifolia
    w, h = ((42, 48) if big else (28, 36))
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    cx = w // 2
    ramp_reed = FOLIAGE_RAMPS["willow_jade" if variant == 1 else "birch_downy"]
    paste_base_anchored(im, mp.reed_tuft, cx, h - 1, ramp_reed, bark, flip_x=(variant == 2), widen_px=(5 if big else 1))
    px = im.load()
    if big:
        stalks = [(-10, 9), (-5, 5), (0, 4), (5, 6), (10, 10)] if variant == 1 else [(-12, 7), (-6, 10), (-1, 4), (4, 8), (9, 5), (13, 11)]
    else:
        stalks = [(-4, 7), (0, 5), (4, 8)] if variant == 1 else [(-6, 6), (-1, 9), (3, 5), (6, 8)]
    for sx_off, top_y in stalks:
        sx = cx + sx_off
        if 2 <= sx < w - 2:
            for y in range(top_y, h - 3):
                px[sx, y] = (ramp_reed[3][0], ramp_reed[3][1], ramp_reed[3][2], 255)
                if sx + 1 < w:
                    px[sx + 1, y] = (ramp_reed[1][0], ramp_reed[1][1], ramp_reed[1][2], 255)
            c0 = top_y + 3
            c1 = min(h - 10, top_y + (12 if big else 8))
            for y in range(c0, c1 + 1):
                px[sx - 1, y] = (154, 96, 62, 255)
                px[sx, y] = (108, 62, 38, 255)
                px[sx + 1, y] = (64, 32, 24, 255)
    return finalize_sprite(im)


def generate_sprite(mp, species, stage, variant):
    if species in ("pinus_sylvestris", "pseudotsuga_menziesii", "abies_alba"):
        return build_conifer_tree(mp, species, stage, variant)
    if species in (
        "betula_pendula", "betula_pubescens", "acacia_senegal",
        "quercus_robur", "salix_babylonica", "acer_pseudoplatanus", "olea_europaea"
    ):
        return build_broadleaf_tree(mp, species, stage, variant)
    if species == "arthrocereus_rondonianus":
        return build_cactus(mp, stage, variant)
    return build_herb_or_crop(mp, species, stage, variant)


def main():
    if not os.path.exists(TILES_PATH):
        sys.exit(f"Missing tiles.png at {TILES_PATH}")
    sheet = Image.open(TILES_PATH).convert("RGBA")
    mp = MasterParts(sheet)

    count = 0
    for out_root in OUT_DIRS:
        for sp in SPECIES_LIST:
            for stage in ("small", "big"):
                stage_dir = os.path.join(out_root, sp, stage)
                os.makedirs(stage_dir, exist_ok=True)
                # Remove any stale >2 variant files (e.g. 3.png, 4.png)
                for old_png in glob.glob(os.path.join(stage_dir, "*.png")):
                    if os.path.basename(old_png) not in ("1.png", "2.png"):
                        os.remove(old_png)
                for var in (1, 2):
                    im = generate_sprite(mp, sp, stage, var)
                    out_path = os.path.join(stage_dir, f"{var}.png")
                    im.save(out_path, "PNG", optimize=True)
                    count += 1
    print(f"Generated {count // len(OUT_DIRS)} unique sprites across {len(OUT_DIRS)} roots ({count} files total).")


if __name__ == "__main__":
    main()
