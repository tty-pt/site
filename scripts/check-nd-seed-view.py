#!/usr/bin/env python3
"""check-nd-seed-view.py CAPTURE PLANTS_DSV

Verify a captured NeverDark WS session ended with the player resting at the
root spawn (0,0,0,0) looking at a coloured, planted map. The facets are
reported separately so a failure names what is wrong:

  spawn   the last `room N at X Y Z W` reply must say `at 0 0 0 0`
  bg      a biome background escape ESC[41m..ESC[47m after that reply
  glyph   a plant glyph (small/big column of PLANTS_DSV) in an ansi fg run

Exit 0 when all hold, 1 otherwise.
"""
import re
import sys


def plant_glyphs(path):
    glyphs = set()
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            f = line.split("|")
            if len(f) >= 6:
                for g in (f[4], f[5]):
                    if g:
                        glyphs.add(g[0])
    return glyphs


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    data = open(argv[1], "rb").read()
    glyphs = plant_glyphs(argv[2])
    rooms = list(re.finditer(rb"room (\d+) at (-?\d+) (-?\d+) (-?\d+) (-?\d+)", data))
    if not rooms:
        print("FAIL spawn: no `room N at X Y Z W` reply captured", file=sys.stderr)
        return 1
    last = rooms[-1]
    where = " ".join(g.decode() for g in last.groups()[1:])
    if where != "0 0 0 0":
        print("FAIL spawn: player ends at %s, want 0 0 0 0" % where, file=sys.stderr)
        return 1
    tail = data[last.end():]
    ok = True
    if not re.search(rb"\x1b\[4[1-7]m", tail):
        print("FAIL bg: no ESC[41m..ESC[47m biome background after the final room reply", file=sys.stderr)
        ok = False
    if glyphs:
        pat = rb"\x1b\[3[0-7]m([" + re.escape("".join(sorted(glyphs)).encode()) + rb"])"
    else:
        pat = rb"$^"
    if not re.search(pat, tail):
        print("FAIL glyph: no plant glyph from %s after the final room reply" % argv[2], file=sys.stderr)
        ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
