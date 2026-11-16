#!/usr/bin/env python3
"""High-craft 16-bit RPG pixel-art flora generator for NeverDark (nd-plant).

Generates 64 RGBA .png sprites (16 species x 2 stages {small, big} x 2 variants {1, 2} = 4 per species)
by extracting, healing, palette-mapping, and crafting iconic silhouettes from the hand-pixelled
trees, foliage crowns, root bases, conifer needle tiers, and herbs in external/nd-seven/resources/tiles.png.

Key craft & layout invariants:
- Exactly 4 PNGs per species: small/1.png, small/2.png, big/1.png, big/2.png.
- Zero grass tufts or ground blades around tree roots: tree bases are pure sculpted bark
  and spreading buttress roots with transparent surroundings so ground grass can be rendered
  independently by the terrain/stage engine.
- Canonical pixel-art dimensions (~48-68x54-72 for big trees, ~32-44x34-46 for small saplings,
  ~16-36x16-38 for herbs/crops/reeds) so every pixel maps 1:1 in world space without giant bloat.
- Strong visual variation between variant 1 and variant 2 within every species and stage.
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
    return g > r + 6 and g > b + 4


def is_apple_red(r, g, b):
    return r > g + 18 and r > 90


def resize_nn(im, nw, nh):
    """Pure pixel-art nearest-neighbor resize avoiding PIL C-extension segfaults on some builds."""
    nw = max(1, int(nw))
    nh = max(1, int(nh))
    sw, sh = im.size
    spx = im.load()
    out = Image.new("RGBA", (nw, nh), (0, 0, 0, 0))
    opx = out.load()
    for y in range(nh):
        sy = min(sh - 1, (y * sh) // nh)
        for x in range(nw):
            sx = min(sw - 1, (x * sw) // nw)
            opx[x, y] = spx[sx, sy]
    return out


def flip_h(im):
    return im.transpose(Image.FLIP_LEFT_RIGHT)


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


def clean_whole_tree(sheet, roi, grass_y, erase_rects=()):
    """Extract an entire hand-pixelled tree sprite while stripping all ground grass pixels below grass_y."""
    im = sheet.crop(roi).copy()
    w, h = im.size
    px = im.load()
    for ex0, ey0, ex1, ey1 in erase_rects:
        for y in range(max(0, ey0), min(h, ey1)):
            for x in range(max(0, ex0), min(w, ex1)):
                px[x, y] = (0, 0, 0, 0)
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 180:
                px[x, y] = (0, 0, 0, 0)
            else:
                if y >= grass_y and (is_leaf_green(r, g, b) or (g > r and g > b)):
                    px[x, y] = (0, 0, 0, 0)
                else:
                    px[x, y] = (r, g, b, 255)
    return keep_largest_component(im, radius=2)


def heal_apples(im):
    """Replace red apple pixels in A3/C3 with surrounding leaf-green shades."""
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


def split_crown_and_bark_base(im, crown_y_limit, base_y_start, raw_im=None, allow_gold_foliage=False):
    """Extract the uncut leafy crown and a 100% grass-free bark root base."""
    w, h = im.size
    px = im.load()

    crown = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    cpx = crown.load()
    for y in range(min(h, crown_y_limit)):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            if allow_gold_foliage:
                is_bark = (y > crown_y_limit - 4) and (r < 110 and g < 85)
            else:
                is_bark = (not is_leaf_green(r, g, b)) and (r > g - 4) and (g < 145 or abs(r - g) <= 16)
            if y > h * 0.42 and is_bark:
                continue
            cpx[x, y] = (r, g, b, 255)

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


def remap_whole_tree(im, foliage_ramp=None, bark_ramp=None, crown_y_split=999, lum_shift=0):
    """Remap foliage and bark of a whole tree sprite cleanly."""
    out = im.copy()
    px = out.load()
    w, h = out.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            lum = int(0.299 * r + 0.587 * g + 0.114 * b) + lum_shift
            is_fol = is_leaf_green(r, g, b) or (g >= r and g >= b and lum < 68) or (y < crown_y_split and r > 110 and g > 80 and b < 75)
            if is_fol:
                if foliage_ramp is not None:
                    t = (lum - 34) / 138.0
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


def remap_foliage_and_bark(im, foliage_ramp=None, bark_ramp=None, lum_shift=0):
    return remap_whole_tree(im, foliage_ramp=foliage_ramp, bark_ramp=bark_ramp, crown_y_split=999, lum_shift=lum_shift)


def paste_crown_lobe(canvas, lobe, dst_x, dst_y, ramp, flip_x=False, lum_shift=0):
    """Paste a remapped crown lobe onto canvas without harsh inner seam outlines."""
    src = flip_h(lobe) if flip_x else lobe
    src = remap_foliage_and_bark(src, foliage_ramp=ramp, lum_shift=lum_shift)
    spx = src.load()
    cpx = canvas.load()
    cw, ch = canvas.size
    sw, sh = src.size
    dst_x = max(1, min(cw - sw - 1, dst_x))
    dst_y = max(1, min(ch - sh - 4, dst_y))

    outline_col = ramp[0]
    mid_shadow = ramp[2]
    mid_leaf = ramp[3]

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
                # Blend interior overlap smoothly into mid foliage rather than drawing a dark ring
                if sy < sh * 0.55:
                    cpx[dx, dy] = (mid_leaf[0], mid_leaf[1], mid_leaf[2], 255)
                else:
                    cpx[dx, dy] = (mid_shadow[0], mid_shadow[1], mid_shadow[2], 255)
            else:
                cpx[dx, dy] = (sr, sg, sb, 255)


def draw_organic_bough(px, w, h, x0, y0, x1, y1, r0, r1, bark_ramp, seed):
    steps = max(abs(x1 - x0), abs(y1 - y0), 1) * 2
    for i in range(steps + 1):
        t = i / float(steps)
        curve = math.sin(t * math.pi) * ((hash2d(i, seed, 17) & 3) - 1.5) * 0.35
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
                elif rel_x < 0.38:
                    col = bark_ramp[4]
                elif rel_x < 0.70:
                    col = bark_ramp[3]
                else:
                    col = bark_ramp[1]
                px[x, y] = (col[0], col[1], col[2], 255)


def add_birch_lenticels(im, y_start, y_end):
    """Add crisp dark horizontal birch bark lenticel notches along white birch trunks."""
    px = im.load()
    w, h = im.size
    for y in range(max(0, y_start), min(h, y_end)):
        for x in range(1, w - 1):
            r, g, b, a = px[x, y]
            if a > 0 and r > 150 and g > 145 and b > 135:
                if (y % 4 == 1 and (x + y) % 3 == 0):
                    px[x, y] = (58, 52, 48, 255)
                elif (y % 4 == 3 and (x - y) % 4 == 0):
                    px[x, y] = (96, 90, 84, 255)


def paste_base_anchored(canvas, base_im, cx, bot_y, foliage_ramp, bark_ramp, flip_x=False, widen_px=0):
    """Paste a grass-free base sprite anchored at (cx, bot_y)."""
    src = flip_h(base_im) if flip_x else base_im
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
        if y < 2 or y >= h - 4:
            continue
        t = dy / max(1.0, float(length - 1))
        cur_x = x_top + sway_dx * (t ** 1.3) + math.sin(t * math.pi * 2.0 + seed) * 0.35
        hw = max(0.5, width_top * (1.0 - 0.55 * (t ** 1.4)))
        jag = ((hash2d(dy // 2, int(x_top), seed) & 3) - 1) * 0.25
        x0 = int(round(cur_x - hw + jag))
        x1 = int(round(cur_x + hw + jag))
        for x in range(max(1, x0), min(w - 1, x1 + 1)):
            if dy == length - 1:
                col = out_c
            elif x == x0 or x == x1:
                col = crevice_c if t > 0.40 else shd_c
            else:
                rel = (x - x0) / max(1.0, float(x1 - x0))
                if t < 0.38 and rel < 0.55:
                    col = hi_c if (dy % 3 != 0) else brt_c
                elif t < 0.74:
                    col = brt_c if rel < 0.5 else mid_c
                else:
                    col = mid_c if rel < 0.5 else shd_c
            px[x, y] = (col[0], col[1], col[2], 255)


class MasterParts:
    def __init__(self, sheet):
        # Whole grass-free 1:1 hand-pixelled master trees from tiles.png (strictly excluding 2x2 blocky a2/b4)
        self.tree_a1_fir = clean_whole_tree(sheet, (336, 0, 400, 72), 66)
        self.tree_a3_pine = heal_apples(clean_whole_tree(sheet, (464, 0, 528, 74), 50))
        self.tree_a4_gold = clean_whole_tree(sheet, (534, 6, 588, 66), 45, [(0, 0, 10, 10)])
        self.tree_b1_oak = clean_whole_tree(sheet, (339, 82, 412, 154), 59, [(60, 0, 73, 15), (0, 60, 15, 72)])
        self.tree_b2_oak = clean_whole_tree(sheet, (418, 78, 477, 141), 50, [(0, 48, 12, 63), (44, 46, 59, 63)])
        self.tree_b3_willow = clean_whole_tree(sheet, (483, 79, 526, 118), 31)
        self.tree_c1_round = clean_whole_tree(sheet, (450, 159, 495, 202), 37)
        self.tree_c3_apple = heal_apples(clean_whole_tree(sheet, (385, 244, 415, 279), 31))
        self.conifer_fir = clean_whole_tree(sheet, (338, 243, 383, 286), 37)
        self.bush_teal = clean_whole_tree(sheet, (480, 131, 511, 156), 21)

        # Separated 1:1 crowns and grass-free bark bases for custom species
        a1 = extract_clean(sheet, (336, 0, 400, 74))
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

        self.a1_crown, self.a1_base = split_crown_and_bark_base(a1, 61, 48)
        self.a3_crown, self.a3_base = split_crown_and_bark_base(a3, 49, 49, raw_im=a3_raw)
        self.a4_crown, self.a4_base = split_crown_and_bark_base(a4, 43, 43, raw_im=a4_raw, allow_gold_foliage=True)
        self.b1_crown, self.b1_base = split_crown_and_bark_base(b1, 58, 46)
        self.b2_crown, self.b2_base = split_crown_and_bark_base(b2, 50, 40)
        self.b3_crown, self.b3_base = split_crown_and_bark_base(b3, 30, 25)
        self.c1_crown, self.c1_base = split_crown_and_bark_base(c1, 36, 28)
        self.c3_crown, self.c3_base = split_crown_and_bark_base(c3, 31, 24, raw_im=c3_raw)

        pbt_raw = extract_clean(sheet, (480, 131, 511, 160))
        self.pine_bush_teal, _ = split_crown_and_bark_base(pbt_raw, 21, 20)

        self.carrot_sm = extract_clean(sheet, (161, 321, 174, 336))
        self.carrot_big = extract_clean(sheet, (176, 328, 192, 350))
        self.tomato_sm = extract_clean(sheet, (245, 369, 265, 398))
        self.tomato_med = extract_clean(sheet, (273, 360, 300, 398))
        self.tomato_big = extract_clean(sheet, (305, 360, 332, 398))
        self.flower_stems = extract_clean(sheet, (465, 220, 479, 240), keep_main=False)
        self.reed_tuft = extract_clean(sheet, (367, 150, 385, 181))
        self.spiky_sm = extract_clean(sheet, (337, 289, 368, 319))


def make_umbrella_pad_1to1(crown, pad_h, ramp):
    """Slice the top pad_h rows of a 1:1 pixel-art crown and sculpt a clean shaded underside."""
    cw, ch = crown.size
    ph = min(ch, pad_h)
    pad = crown.crop((0, 0, cw, ph)).copy()
    pad = remap_foliage_and_bark(pad, foliage_ramp=ramp)
    px = pad.load()
    out_c = (ramp[0][0], ramp[0][1], ramp[0][2], 255)
    shd_c = (ramp[1][0], ramp[1][1], ramp[1][2], 255)
    mid_c = (ramp[2][0], ramp[2][1], ramp[2][2], 255)
    for x in range(cw):
        # Taper bottom 2 rows at extreme edges so pad has an organic lenticular silhouette
        edge_dist = min(x, cw - 1 - x)
        trim_bot = 2 if edge_dist < 4 else (1 if edge_dist < 9 else 0)
        bot = ph - 1 - trim_bot
        for y in range(bot + 1, ph):
            px[x, y] = (0, 0, 0, 0)
        if bot >= 2 and px[x, bot][3] > 0:
            px[x, bot] = out_c
            if px[x, bot - 1][3] > 0:
                px[x, bot - 1] = shd_c
            if px[x, bot - 2][3] > 0:
                px[x, bot - 2] = mid_c
    return pad


def finalize_sprite(canvas):
    w, h = canvas.size
    bb = canvas.getchannel("A").getbbox()
    if not bb:
        return canvas
    x0, y0, x1, y1 = bb
    cx = w // 2
    half_w = max(cx - x0, x1 - cx) + 1
    new_w = half_w * 2
    new_h = y1 - y0
    out = Image.new("RGBA", (new_w, new_h), (0, 0, 0, 0))
    out.paste(canvas.crop((cx - half_w, y0, cx + half_w, y1)), (0, 0))
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
        # Variant 1: Classic pillow-lobed English Oak (native 1:1 tree_b1_oak / tree_c1_round)
        # Variant 2: Multi-lobed golden-summer English Oak (native 1:1 b2_crown + c1_crown / tree_c3_apple)
        if big:
            if variant == 1:
                im = remap_whole_tree(mp.tree_b1_oak, FOLIAGE_RAMPS["oak_emerald"], BARK_RAMPS["oak_brown"], crown_y_split=48)
                return finalize_sprite(im)
            else:
                w, h = 64, 62
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                ramp = FOLIAGE_RAMPS["oak_golden"]
                bark = BARK_RAMPS["oak_dark"]
                draw_organic_bough(im.load(), w, h, cx, h - 10, cx - 12, 24, 3.2, 1.8, bark, 2)
                draw_organic_bough(im.load(), w, h, cx, h - 10, cx + 12, 24, 3.2, 1.8, bark, 6)
                paste_base_anchored(im, mp.b1_base, cx, h - 1, ramp, bark, flip_x=True)
                paste_crown_lobe(im, mp.c1_crown, cx - 28, 12, ramp, flip_x=False, lum_shift=-4)
                paste_crown_lobe(im, mp.c1_crown, cx - 10, 12, ramp, flip_x=True, lum_shift=-2)
                paste_crown_lobe(im, mp.b2_crown, cx - mp.b2_crown.width // 2, 1, ramp, flip_x=False, lum_shift=+4)
                return finalize_sprite(im)
        else:
            if variant == 1:
                im = remap_whole_tree(mp.tree_c1_round, FOLIAGE_RAMPS["oak_emerald"], BARK_RAMPS["oak_brown"], crown_y_split=30)
                return finalize_sprite(im)
            else:
                im = remap_whole_tree(mp.tree_c3_apple, FOLIAGE_RAMPS["oak_golden"], BARK_RAMPS["oak_dark"], crown_y_split=26)
                return finalize_sprite(im)

    if species == "acer_pseudoplatanus":
        # Variant 1: Sun-lit gold-green symmetric Sycamore Maple (native 1:1 tree_b2_oak)
        # Variant 2: Rich autumn amber-crimson Maple (native 1:1 tree_b1_oak / tree_c3_apple)
        if big:
            if variant == 1:
                im = remap_whole_tree(flip_h(mp.tree_b2_oak), FOLIAGE_RAMPS["maple_gold_green"], BARK_RAMPS["oak_brown"], crown_y_split=42)
                return finalize_sprite(im)
            else:
                im = remap_whole_tree(flip_h(mp.tree_b1_oak), FOLIAGE_RAMPS["maple_amber"], BARK_RAMPS["oak_dark"], crown_y_split=48)
                return finalize_sprite(im)
        else:
            if variant == 1:
                im = remap_whole_tree(flip_h(mp.tree_c1_round), FOLIAGE_RAMPS["maple_gold_green"], BARK_RAMPS["oak_brown"], crown_y_split=30)
                return finalize_sprite(im)
            else:
                im = remap_whole_tree(flip_h(mp.tree_c3_apple), FOLIAGE_RAMPS["maple_amber"], BARK_RAMPS["oak_dark"], crown_y_split=26)
                return finalize_sprite(im)

    if species in ("betula_pendula", "betula_pubescens"):
        # Silver Birch (betula_pendula) & Downy Birch (betula_pubescens):
        # Slender bright white bark trunk with dark lenticel bands and airy 1:1 serrated crown
        bark = BARK_RAMPS["birch_white"]
        ramp1 = FOLIAGE_RAMPS["birch_lime" if species == "betula_pendula" else "birch_downy"]
        ramp2 = FOLIAGE_RAMPS["birch_gold" if species == "betula_pendula" else "willow_jade"]
        if big:
            if variant == 1:
                w, h = 60, 64
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 8, cx - 1, 22, 2.5, 1.4, bark, 1)
                draw_organic_bough(im.load(), w, h, cx, h - 18, cx - 10, 28, 1.8, 1.1, bark, 3)
                draw_organic_bough(im.load(), w, h, cx, h - 20, cx + 10, 26, 1.8, 1.1, bark, 5)
                paste_base_anchored(im, mp.a4_base, cx, h - 1, ramp1, bark, flip_x=False)
                add_birch_lenticels(im, 20, h - 2)
                paste_crown_lobe(im, mp.a3_crown, cx - mp.a3_crown.width // 2, 1, ramp1, flip_x=False, lum_shift=+4)
                return finalize_sprite(im)
            else:
                w, h = 62, 62
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx - 1, h - 8, cx - 10, 22, 2.2, 1.2, bark, 2)
                draw_organic_bough(im.load(), w, h, cx + 1, h - 8, cx + 10, 20, 2.2, 1.2, bark, 8)
                paste_base_anchored(im, mp.a4_base, cx, h - 1, ramp2, bark, flip_x=True)
                add_birch_lenticels(im, 20, h - 2)
                paste_crown_lobe(im, mp.c1_crown, cx - 26, 8, ramp2, flip_x=False, lum_shift=-2)
                paste_crown_lobe(im, mp.c1_crown, cx - 10, 2, ramp2, flip_x=True, lum_shift=+5)
                return finalize_sprite(im)
        else:
            if variant == 1:
                im = remap_whole_tree(mp.tree_b3_willow, ramp1, bark, crown_y_split=24)
                add_birch_lenticels(im, 14, im.height - 2)
                return finalize_sprite(im)
            else:
                im = remap_whole_tree(flip_h(mp.tree_b3_willow), ramp2, bark, crown_y_split=24)
                add_birch_lenticels(im, 14, im.height - 2)
                return finalize_sprite(im)

    if species == "salix_babylonica":
        # Weeping Willow: cascading 1:1 jade/golden-green frond curtains under native 1:1 crown dome
        bark = BARK_RAMPS["oak_brown"]
        if big:
            ramp = FOLIAGE_RAMPS["willow_jade" if variant == 1 else "willow_weeping_gold"]
            w, h = (64, 62) if variant == 1 else (66, 64)
            im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            cx = w // 2
            px = im.load()
            for dx in range(-20, 21, 5):
                draw_willow_curtain_lock(px, w, h, cx + dx, 22 + abs(dx) // 4, 24 - abs(dx) // 4, 1.8, (dx / 18.0), ramp, 1 + dx)
            draw_organic_bough(px, w, h, cx, h - 10, cx - 12, 22, 3.2, 1.8, bark, 1)
            draw_organic_bough(px, w, h, cx, h - 10, cx + 12, 22, 3.2, 1.8, bark, 5)
            paste_base_anchored(im, mp.a4_base, cx, h - 1, ramp, bark, flip_x=(variant == 2))
            paste_crown_lobe(im, mp.b2_crown, cx - mp.b2_crown.width // 2, 1, ramp, flip_x=(variant == 2), lum_shift=+4)
            px = im.load()
            offsets = [-22, -18, -14, -10, -6, 6, 10, 14, 18, 22]
            for idx_l, dx in enumerate(offsets):
                start_y = 18 + (abs(dx) // 5) - (idx_l % 2) * 2
                lock_len = 26 + ((hash2d(dx, variant, 41) % 5) - 2) - max(0, (10 - abs(dx)))
                sway = (-0.8 if dx < 0 else 0.8) if variant == 1 else 1.8
                draw_willow_curtain_lock(px, w, h, cx + dx, start_y, lock_len, 1.7, sway, ramp, 17 + idx_l)
            return finalize_sprite(im)
        else:
            if variant == 1:
                im = remap_whole_tree(mp.tree_b3_willow, FOLIAGE_RAMPS["willow_jade"], bark, crown_y_split=24)
                return finalize_sprite(im)
            else:
                im = remap_whole_tree(flip_h(mp.tree_b3_willow), FOLIAGE_RAMPS["willow_weeping_gold"], bark, crown_y_split=24)
                return finalize_sprite(im)

    if species == "acacia_senegal":
        # Savanna Umbrella Thorn Acacia: wide flat-topped 1:1 canopy pads with slender angled V-boughs
        bark = BARK_RAMPS["oak_brown"]
        ramp = FOLIAGE_RAMPS["savanna_acacia" if variant == 1 else "oak_golden"]
        if big:
            w, h = (68, 54) if variant == 1 else (66, 56)
            im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            cx = w // 2
            pad_main = make_umbrella_pad_1to1(mp.b2_crown, 20, ramp)
            pad_wing = make_umbrella_pad_1to1(mp.c1_crown, 16, ramp)
            if variant == 1:
                draw_organic_bough(im.load(), w, h, cx, h - 8, cx - 18, 18, 2.4, 1.2, bark, 1)
                draw_organic_bough(im.load(), w, h, cx, h - 8, cx + 18, 18, 2.4, 1.2, bark, 4)
                draw_organic_bough(im.load(), w, h, cx, h - 12, cx, 14, 2.0, 1.1, bark, 7)
                paste_base_anchored(im, mp.a3_base, cx, h - 1, ramp, bark, flip_x=False)
                paste_crown_lobe(im, pad_wing, cx - 30, 14, ramp, flip_x=False, lum_shift=-4)
                paste_crown_lobe(im, pad_wing, cx - 8, 14, ramp, flip_x=True, lum_shift=-6)
                paste_crown_lobe(im, pad_main, cx - pad_main.width // 2, 2, ramp, flip_x=False, lum_shift=+5)
            else:
                draw_organic_bough(im.load(), w, h, cx, h - 8, cx - 16, 22, 2.4, 1.2, bark, 2)
                draw_organic_bough(im.load(), w, h, cx, h - 8, cx + 14, 14, 2.5, 1.2, bark, 8)
                paste_base_anchored(im, mp.a4_base, cx, h - 1, ramp, bark, flip_x=True)
                paste_crown_lobe(im, pad_wing, cx - 28, 16, ramp, flip_x=False, lum_shift=-4)
                paste_crown_lobe(im, pad_main, cx - 18, 2, ramp, flip_x=True, lum_shift=+6)
            return finalize_sprite(im)
        else:
            w, h = (46, 36) if variant == 1 else (44, 38)
            im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            cx = w // 2
            draw_organic_bough(im.load(), w, h, cx, h - 6, cx - 10, 14, 1.8, 1.0, bark, 1)
            draw_organic_bough(im.load(), w, h, cx, h - 6, cx + 10, 12, 1.8, 1.0, bark, 5)
            paste_base_anchored(im, mp.c1_base, cx, h - 1, ramp, bark, flip_x=(variant == 2))
            pad_sm = make_umbrella_pad_1to1(mp.c1_crown, 16, ramp)
            paste_crown_lobe(im, pad_sm, cx - pad_sm.width // 2, 2, ramp, flip_x=(variant == 2), lum_shift=+4)
            return finalize_sprite(im)

    # olea_europaea (Mediterranean Olive: gnarled silver-ash trunk & 1:1 sage-olive crown)
    bark = BARK_RAMPS["olive_ash"]
    ramp = FOLIAGE_RAMPS["olive_sage" if variant == 1 else "birch_downy"]
    if big:
        if variant == 1:
            im = remap_whole_tree(mp.tree_b2_oak, ramp, bark, crown_y_split=40)
            return finalize_sprite(im)
        else:
            w, h = 60, 58
            im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            cx = w // 2
            draw_organic_bough(im.load(), w, h, cx - 1, h - 9, cx - 14, 22, 3.0, 1.6, bark, 3)
            draw_organic_bough(im.load(), w, h, cx + 1, h - 9, cx + 13, 20, 3.0, 1.6, bark, 9)
            paste_base_anchored(im, mp.b2_base, cx, h - 1, ramp, bark, flip_x=True)
            paste_crown_lobe(im, mp.c1_crown, cx - 26, 10, ramp, flip_x=False, lum_shift=-3)
            paste_crown_lobe(im, mp.c1_crown, cx - 10, 4, ramp, flip_x=True, lum_shift=+5)
            return finalize_sprite(im)
    else:
        if variant == 1:
            im = remap_whole_tree(mp.tree_c1_round, ramp, bark, crown_y_split=30)
            return finalize_sprite(im)
        else:
            im = remap_whole_tree(mp.tree_c3_apple, ramp, bark, crown_y_split=26)
            return finalize_sprite(im)


def build_conifer_tree(mp, species, stage, variant):
    big = (stage == "big")

    if species == "pinus_sylvestris":
        # Scots Pine: warm red-orange bark trunk with high 1:1 conifer crown
        bark = BARK_RAMPS["pine_red"]
        ramp = FOLIAGE_RAMPS["pine_scots" if variant == 1 else "fir_douglas"]
        if big:
            if variant == 1:
                im = remap_whole_tree(mp.tree_a3_pine, ramp, bark, crown_y_split=46)
                return finalize_sprite(im)
            else:
                im = remap_whole_tree(flip_h(mp.tree_a3_pine), ramp, bark, crown_y_split=46)
                return finalize_sprite(im)
        else:
            if variant == 1:
                im = remap_whole_tree(mp.conifer_fir, ramp, bark, crown_y_split=36)
                return finalize_sprite(im)
            else:
                w, h = 38, 36
                im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                cx = w // 2
                draw_organic_bough(im.load(), w, h, cx, h - 5, cx, 12, 2.2, 1.2, bark, 4)
                paste_base_anchored(im, mp.c3_base, cx, h - 1, ramp, bark, flip_x=True)
                paste_crown_lobe(im, mp.pine_bush_teal, cx - mp.pine_bush_teal.width // 2, 1, ramp, flip_x=True, lum_shift=+3)
                return finalize_sprite(im)

    # pseudotsuga_menziesii (Douglas Fir) & abies_alba (Silver Fir)
    if species == "pseudotsuga_menziesii":
        ramp1 = FOLIAGE_RAMPS["fir_douglas"]
        ramp2 = FOLIAGE_RAMPS["oak_emerald"]
        bark = BARK_RAMPS["oak_brown"]
    else:
        ramp1 = FOLIAGE_RAMPS["fir_silver"]
        ramp2 = FOLIAGE_RAMPS["birch_downy"]
        bark = BARK_RAMPS["olive_ash"]

    if big:
        if variant == 1:
            im = remap_whole_tree(mp.tree_a1_fir, ramp1, bark, crown_y_split=62)
            return finalize_sprite(im)
        else:
            # Layered conical alpine fir using native 1:1 flip_h(tree_a1_fir)
            im = remap_whole_tree(flip_h(mp.tree_a1_fir), ramp2, bark, crown_y_split=62)
            return finalize_sprite(im)
    else:
        if variant == 1:
            im = remap_whole_tree(mp.conifer_fir, ramp1, bark, crown_y_split=36)
            return finalize_sprite(im)
        else:
            im = remap_whole_tree(flip_h(mp.conifer_fir), ramp2, bark, crown_y_split=36)
            return finalize_sprite(im)


def build_cactus(mp, stage, variant):
    big = (stage == "big")
    ramp = FOLIAGE_RAMPS["oak_emerald" if variant == 1 else "fir_douglas"]
    bark = BARK_RAMPS["oak_brown"]
    w, h = ((32, 48) if variant == 1 else (34, 46)) if big else ((22, 30) if variant == 1 else (24, 28))
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    cx = w // 2
    paste_base_anchored(im, mp.c3_base, cx, h - 1, ramp, bark, flip_x=(variant == 2))
    px = im.load()
    out_c, shd_c, low_c, mid_c, brt_c, hi_c, _ = ramp

    top_y = 4 if big else 5
    rx = 4 if big else 3
    for y in range(top_y, h - 2):
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
            [(-1, int(h * 0.46), int(h * 0.18)), (1, int(h * 0.62), int(h * 0.28))]
        )
    else:
        arm_configs = (
            [(-1, int(h * 0.60), int(h * 0.34))]
            if variant == 1 else
            [(1, int(h * 0.58), int(h * 0.32))]
        )

    ar_w = 2
    for adir, ay_base, ay_top in arm_configs:
        ax = cx + adir * (rx + (5 if big else 4))
        for x in range(min(cx, ax), max(cx, ax) + 1):
            for dy in range(-ar_w, ar_w + 1):
                yy = ay_base + dy
                if 0 <= yy < h - 3 and 0 <= x < w:
                    col = out_c if abs(dy) == ar_w else (brt_c if dy < 0 else shd_c)
                    px[x, yy] = (col[0], col[1], col[2], 255)
        for y in range(ay_top, ay_base + ar_w):
            for dx in range(-ar_w, ar_w + 1):
                xx = ax + dx
                if 0 <= xx < w and 0 <= y < h - 3:
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
        w, h = ((30, 26) if big else (20, 18))
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        cx = w // 2
        if big:
            offsets = [(-5, -1, mp.carrot_big, False), (5, -1, mp.carrot_big, True), (0, 0, mp.carrot_big, False)] if variant == 1 else [(-6, -2, mp.carrot_sm, True), (6, -2, mp.carrot_sm, False), (0, 0, mp.carrot_big, False)]
        else:
            offsets = [(0, 0, mp.carrot_sm, False)] if variant == 1 else [(-3, -1, mp.carrot_sm, True), (3, 0, mp.carrot_sm, False)]
        for ox, oy, spr, fl in offsets:
            s = flip_h(spr) if fl else spr
            im.alpha_composite(s, (cx + ox - s.width // 2, h - s.height + oy))
        return finalize_sprite(im)

    if species == "solanum_lycopersicum":
        if big:
            im = mp.tomato_big if variant == 1 else flip_h(mp.tomato_big)
            return finalize_sprite(resize_nn(im, 26, 32))
        else:
            im = mp.tomato_sm if variant == 1 else resize_nn(mp.tomato_med, 20, 26)
            return finalize_sprite(im)

    if species in ("linum_usitatissimum", "bellis_perennis"):
        w, h = ((30, 28) if big else (22, 20))
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        cx = w // 2
        base_tuft = resize_nn(mp.reed_tuft if species == "linum_usitatissimum" else mp.spiky_sm, 18 if big else 14, 14 if big else 11)
        paste_base_anchored(im, base_tuft, cx, h - 1, ramp, bark, flip_x=(variant == 2))
        stems = resize_nn(mp.flower_stems, 12, 16 if big else 13)
        if big:
            im.alpha_composite(stems, (cx - stems.width - 1, h - stems.height - 2))
            im.alpha_composite(flip_h(stems), (cx + 1, h - stems.height - 2))
        im.alpha_composite(stems, (cx - stems.width // 2, h - stems.height - (3 if big else 1)))

        px = im.load()
        if big:
            blossoms = [(-6, 7), (-2, 5), (2, 4), (6, 6), (-3, 10), (4, 10)] if variant == 1 else [(-7, 6), (-3, 8), (0, 4), (4, 6), (7, 8), (2, 11)]
        else:
            blossoms = [(-3, 5), (1, 4), (4, 6)] if variant == 1 else [(-4, 5), (0, 7), (3, 4)]
        for bx_off, by in blossoms:
            bx = cx + bx_off
            if 2 <= bx < w - 2 and 2 <= by < h - 3:
                if species == "linum_usitatissimum":
                    petal = (96, 168, 248, 255) if variant == 1 else (138, 198, 255, 255)
                    center = (250, 232, 110, 255)
                else:
                    petal = (248, 250, 252, 255) if variant == 1 else (255, 224, 238, 255)
                    center = (248, 198, 54, 255)
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    px[bx + dx, by + dy] = petal
                px[bx, by] = center
        return finalize_sprite(im)

    # typha_latifolia (Bulrush / Cattail)
    w, h = ((28, 34) if big else (22, 26))
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    cx = w // 2
    ramp_reed = FOLIAGE_RAMPS["willow_jade" if variant == 1 else "birch_downy"]
    tuft = resize_nn(mp.reed_tuft, 16 if big else 13, 14 if big else 11)
    paste_base_anchored(im, tuft, cx, h - 1, ramp_reed, bark, flip_x=(variant == 2), widen_px=(2 if big else 0))
    px = im.load()
    if big:
        stalks = [(-6, 6), (-2, 4), (2, 5), (6, 7)] if variant == 1 else [(-7, 5), (-3, 7), (1, 4), (5, 6), (8, 8)]
    else:
        stalks = [(-3, 5), (0, 4), (3, 6)] if variant == 1 else [(-4, 5), (0, 6), (4, 4)]
    for sx_off, top_y in stalks:
        sx = cx + sx_off
        if 2 <= sx < w - 2:
            for y in range(top_y, h - 2):
                px[sx, y] = (ramp_reed[3][0], ramp_reed[3][1], ramp_reed[3][2], 255)
                if sx + 1 < w:
                    px[sx + 1, y] = (ramp_reed[1][0], ramp_reed[1][1], ramp_reed[1][2], 255)
            c0 = top_y + 2
            c1 = min(h - 8, top_y + (8 if big else 6))
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
