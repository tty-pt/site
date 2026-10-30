#!/usr/bin/env python3
"""
Inspect and verify collision maps from ~/seven-ways against Seven world object models.
Extracts collision footprints for rocks, stones, boulders, trees, and terrain.
"""

import xml.etree.ElementTree as ET
from PIL import Image

def main():
    tsx_path = "/home/quirinpa/seven-ways/resources/tiles.tsx"
    col_path = "/home/quirinpa/seven-ways/map/col0.png"
    tiles_path = "/home/quirinpa/seven-ways/resources/tiles.png"

    tree = ET.parse(tsx_path)
    col_set = set()
    for tile in tree.getroot().findall("tile"):
        for prop in tile.findall("./properties/property"):
            if prop.attrib.get("name") == "col" and prop.attrib.get("value") == "true":
                col_set.add(int(tile.attrib["id"]))

    print(f"Total collidable tiles in tiles.tsx: {len(col_set)}")

    # Sprites in Seven atlas
    sprites = {
        "NDS_ROCK_WHITE_BIG": (366, 0, 50, 62),
        "NDS_ROCK_TAN_BIG":   (0, 256, 57, 56),
        "NDS_ROCK_TAN":       (57, 256, 48, 45),
        "NDS_ROCK_GREY":      (182, 256, 30, 32),
        "NDS_STONE_GREY":     (357, 256, 25, 24),
        "NDS_STONE_TINY":     (406, 256, 15, 18),
    }

    im_tiles = Image.open(tiles_path)
    im_col = Image.open(col_path)

    for name, (sx, sy, sw, sh) in sprites.items():
        tx0 = sx // 16
        tx1 = (sx + sw - 1) // 16
        ty0 = sy // 16
        ty1 = (sy + sh - 1) // 16
        col_tiles = []
        for ty in range(ty0, ty1 + 1):
            for tx in range(tx0, tx1 + 1):
                tid = ty * 37 + tx
                p = im_col.getpixel((tx, ty))
                is_col = (p[0] > 0 if isinstance(p, tuple) else p > 0)
                if is_col:
                    col_tiles.append((tx, ty))
        print(f"Sprite {name:18s} ({sw:2d}x{sh:2d} px): tiles ({tx0}..{tx1}, {ty0}..{ty1}), col tiles={col_tiles}")

if __name__ == "__main__":
    main()
