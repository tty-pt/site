#!/bin/sh
set -eu
REPO_ROOT="$(pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT HUP INT TERM
gcc -Wall -Wextra -Werror -O3 \
    -I"$REPO_ROOT/external/libnoise/include" \
    -I"$REPO_ROOT/external/nd-world/include" \
    -o "$out/test_terrain_3d" tests/unit/test_terrain_3d.c \
    -lxxhash
"$out/test_terrain_3d"
