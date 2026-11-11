#!/bin/sh
set -eu
REPO_ROOT="$(pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT HUP INT TERM

gcc -Wall -Wextra -Werror -O3 \
    -I"$REPO_ROOT/external/libareole/include" \
    -I"$REPO_ROOT/external/libchaff/include" \
    -I"$REPO_ROOT/external/nd-world/include" \
    -o "$out/test_lod_zoom_consistency" \
    "$REPO_ROOT/external/libareole/src/areole.c" \
    "$REPO_ROOT/tests/unit/test_lod_zoom_consistency.c" \
    -lm -lxxhash

"$out/test_lod_zoom_consistency"
