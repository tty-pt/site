#!/usr/bin/env python3
"""Generate mods/nd/ux/nd_seven_atlas.h from the seven-ways art.

The browser bridge only offers lam_canvas_flush(RGBA buffer), so the sprites
the 2D "seven" stage needs must live in WASM memory.  This script crops the
sprites we use out of external/nd-seven/resources/{lamb,tiles}.png, packs
them into one palette-indexed atlas, run-length encodes it and emits a pure-C
header.  The header is committed; rerun this script (make seven-atlas) after
changing the sprite list or the source art.

usage: gen-seven-atlas.py [--preview out.png]
"""
import hashlib
import os
import sys
import zlib
import struct

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.exit("gen-seven-atlas.py needs Pillow (python3-pil)")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
RES = os.path.join(ROOT, "external", "nd-seven", "resources")
OUT = os.path.join(ROOT, "mods", "nd", "ux", "nd_seven_atlas.h")
ATLAS_W = 512

# name -> (sheet, (x0, y0, x1, y1), trim[, erase-rects])
# trim=True crops to the opaque bounding box inside the ROI.
SPRITES = [
    # --- ground tiles (16x16, exact cells of tiles.png) ---
    ("GRASS",       "tiles", (16, 0, 32, 16), False),
    ("PATH",        "tiles", (48, 336, 64, 352), False),
    ("PATH2",       "tiles", (64, 336, 80, 352), False),
    ("PATH3",       "tiles", (80, 336, 96, 352), False),
    ("PATH4",       "tiles", (96, 336, 112, 352), False),
    ("WALL",        "tiles", (48, 384, 64, 400), False),
    ("WALL2",       "tiles", (64, 384, 80, 400), False),
    ("WALL3",       "tiles", (80, 384, 96, 400), False),
    ("WALL4",       "tiles", (96, 384, 112, 400), False),
    # --- small ground decoration ---
    ("DECOR_YEL",   "tiles", (528, 0, 548, 16), True),
    ("DECOR_YEL2",  "tiles", (446, 64, 466, 80), True),
    ("DECOR_ORG",   "tiles", (402, 82, 418, 96), True),
    ("DECOR_PINK",  "tiles", (576, 144, 592, 164), True),
    # --- trees (2x1 footprint, drawn bottom-centre anchored) ---
    ("TREE_OAK",    "tiles", (400, 0, 464, 66), True),
    ("TREE_BIG",    "tiles", (336, 0, 400, 74), True),
    ("TREE_APPLE",  "tiles", (464, 0, 528, 76), True),
    ("TREE_APPLE2", "tiles", (534, 8, 592, 74), True, [(528, 0, 542, 14)]),
    ("TREE_ROUND",  "tiles", (480, 80, 528, 120), True),
    # --- bushes (1x1 footprint) ---
    ("BUSH_DARK",   "tiles", (528, 84, 560, 112), True),
    ("BUSH_TEAL",   "tiles", (478, 126, 512, 158), True),
    # --- rocks ---
    ("ROCK_TAN_BIG",   "tiles", (340, 320, 400, 380), True),
    ("ROCK_WHITE_BIG", "tiles", (536, 334, 590, 396), True),
    ("ROCK_TAN",       "tiles", (400, 316, 449, 366), True),
    ("ROCK_TAN_SM",    "tiles", (448, 318, 482, 350), True),
    ("ROCK_TAN_TINY",  "tiles", (484, 318, 512, 350), True),
    ("ROCK_WHITE",     "tiles", (486, 350, 522, 398), True),
    ("ROCK_GREY",      "tiles", (448, 352, 482, 384), True),
    # --- stones (small loose minerals / ground stones) ---
    ("STONE_BLUE",     "tiles", (368, 384, 402, 416), True),
    ("STONE_BLUE_SM",  "tiles", (436, 380, 462, 402), True),
    ("STONE_GREY",     "tiles", (402, 372, 430, 400), True),
    ("STONE_FLAT",     "tiles", (528, 318, 546, 338), True),
    ("STONE_TINY",     "tiles", (464, 380, 484, 402), True),
    # --- the lamb: whole 192x256 sheet, 6 cols x 8 rows of 32x32 ---
    ("LAMB",        "lamb",  (0, 0, 192, 256), False),
]


def load(name):
    return Image.open(os.path.join(RES, name + ".png")).convert("RGBA")


def crop(im, roi, trim, erase=()):
    im = im.copy()
    for r in erase:
        im.paste((0, 0, 0, 0), r)
    c = im.crop(roi)
    if trim:
        bb = c.getchannel("A").point(lambda a: 255 if a else 0).getbbox()
        if bb is None:
            raise SystemExit("empty sprite roi %r" % (roi,))
        c = c.crop(bb)
    return c


def pack(images):
    """Shelf-pack images into an ATLAS_W wide sheet; returns (placements, h)."""
    order = sorted(range(len(images)), key=lambda i: -images[i].height)
    x = y = shelf_h = 0
    place = [None] * len(images)
    for i in order:
        w, h = images[i].size
        if x + w > ATLAS_W:
            x, y, shelf_h = 0, y + shelf_h, 0
        place[i] = (x, y)
        x += w
        shelf_h = max(shelf_h, h)
    return place, y + shelf_h


def main():
    preview = None
    if "--preview" in sys.argv:
        preview = sys.argv[sys.argv.index("--preview") + 1]

    sheets = {n: load(n) for n in ("tiles", "lamb")}
    images = [crop(sheets[e[1]], e[2], e[3], e[4] if len(e) > 4 else ())
              for e in SPRITES]
    place, atlas_h = pack(images)

    atlas = Image.new("RGBA", (ATLAS_W, atlas_h), (0, 0, 0, 0))
    for im, (px, py) in zip(images, place):
        atlas.paste(im, (px, py))

    # Palette: index 0 is fully transparent; every distinct ARGB is an entry.
    pal = {(0, 0, 0, 0): 0}
    px = atlas.load()
    idx = bytearray()
    for y in range(atlas_h):
        for x in range(ATLAS_W):
            p = px[x, y]
            if p[3] == 0:
                p = (0, 0, 0, 0)
            if p not in pal:
                pal[p] = len(pal)
            idx.append(pal[p])
    if len(pal) > 256:
        # Reduce to 255 colours + transparent, keeping alpha.
        q = atlas.quantize(colors=255, method=Image.Quantize.FASTOCTREE)
        qpal = q.getpalette()
        qpx = q.load()
        pal = {(0, 0, 0, 0): 0}
        idx = bytearray()
        remap = {}
        for y in range(atlas_h):
            for x in range(ATLAS_W):
                a = px[x, y][3]
                if a == 0:
                    idx.append(0)
                    continue
                k = qpx[x, y]
                col = (qpal[3 * k], qpal[3 * k + 1], qpal[3 * k + 2], a)
                if col not in pal:
                    pal[col] = len(pal)
                idx.append(pal[col])
        if len(pal) > 256:
            raise SystemExit("palette overflow: %d" % len(pal))

    palette = [None] * len(pal)
    for col, i in pal.items():
        palette[i] = col

    # RLE: (run-1, value) byte pairs, runs up to 256.
    rle = bytearray()
    i = 0
    n = len(idx)
    while i < n:
        v = idx[i]
        j = i
        while j < n and idx[j] == v and j - i < 256:
            j += 1
        rle += bytes((j - i - 1, v))
        i = j

    srcsha = hashlib.sha256()
    for name in ("tiles", "lamb"):
        with open(os.path.join(RES, name + ".png"), "rb") as f:
            srcsha.update(f.read())

    names = [s[0] for s in SPRITES]
    out = []
    out.append("/* GENERATED by scripts/gen-seven-atlas.py -- do not edit.\n"
               " * source sha256 (tiles.png+lamb.png): %s\n"
               " * Palette-indexed, run-length encoded sprite atlas for the 2D\n"
               " * seven stage (see nd_seven.c).  Index 0 is transparent. */\n"
               % srcsha.hexdigest())
    out.append("#ifndef ND_SEVEN_ATLAS_H\n#define ND_SEVEN_ATLAS_H\n\n#include <stdint.h>\n")
    out.append("#define ND_SEVEN_ATLAS_W %d\n#define ND_SEVEN_ATLAS_H_PX %d\n" % (ATLAS_W, atlas_h))
    out.append("#define ND_SEVEN_PALETTE_N %d\n" % len(palette))
    out.append("enum nd_seven_sprite {")
    for nme in names:
        out.append("\tNDS_%s," % nme)
    out.append("\tNDS_COUNT\n};\n")
    out.append("typedef struct { uint16_t x, y, w, h; } nd_seven_sprite_rect_t;\n")
    out.append("static const nd_seven_sprite_rect_t nd_seven_sprite_rects[NDS_COUNT] = {")
    for nme, im, (ax, ay) in zip(names, images, place):
        out.append("\t[NDS_%s] = { %d, %d, %d, %d }," % (nme, ax, ay, im.width, im.height))
    out.append("};\n")
    out.append("/* 0xAARRGGBB-in-memory-order pixels (little endian R,G,B,A bytes as the canvas wants). */")
    out.append("static const uint32_t nd_seven_palette[ND_SEVEN_PALETTE_N] = {")
    row = []
    for (r, g, b, a) in palette:
        # canvas buffer is RGBA bytes => little-endian uint32 = A<<24|B<<16|G<<8|R
        row.append("0x%08X" % ((a << 24) | (b << 16) | (g << 8) | r))
        if len(row) == 8:
            out.append("\t" + ", ".join(row) + ",")
            row = []
    if row:
        out.append("\t" + ", ".join(row) + ",")
    out.append("};\n")
    out.append("#define ND_SEVEN_RLE_LEN %d" % len(rle))
    out.append("static const uint8_t nd_seven_rle[ND_SEVEN_RLE_LEN] = {")
    row = []
    for b in rle:
        row.append("%d" % b)
        if len(row) == 32:
            out.append("\t" + ",".join(row) + ",")
            row = []
    if row:
        out.append("\t" + ",".join(row) + ",")
    out.append("};\n\n#endif /* ND_SEVEN_ATLAS_H */\n")

    with open(OUT, "w") as f:
        f.write("\n".join(out))
    print("wrote %s: %d sprites, atlas %dx%d, %d colours, %d RLE bytes"
          % (os.path.relpath(OUT, ROOT), len(names), ATLAS_W, atlas_h, len(palette), len(rle)))

    if preview:
        bg = Image.new("RGBA", atlas.size, (255, 0, 255, 255))
        bg.alpha_composite(atlas)
        bg = bg.resize((atlas.width * 2, atlas.height * 2), Image.NEAREST)
        bg.save(preview)


if __name__ == "__main__":
    main()
