#!/bin/sh
set -eu
REPO_ROOT="$(pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT HUP INT TERM
gcc -Wall -Wextra -Werror -O1 \
    -I"$REPO_ROOT/external/nd-world/include" \
    -I"$REPO_ROOT/external/libnoise/include" \
    -I"$REPO_ROOT/include" \
    -I"$REPO_ROOT/external/axil-nd/include" \
    -I"$REPO_ROOT/external/nd-seven/include" \
    -I"$REPO_ROOT/external/libxylem/include" \
    -I"$REPO_ROOT/mods/nd/ux" \
    -o "$out/test_seven_world" \
    tests/unit/test_seven_world.c \
    mods/nd/ux/nd_seven_ground.c \
    mods/nd/ux/nd_seven_motion.c \
    mods/nd/ux/nd_seven_objects.c \
    mods/nd/ux/nd_seven_atlas_draw.c \
    -lxxhash
"$out/test_seven_world"
