#!/bin/sh
set -eu
REPO_ROOT="$(pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT HUP INT TERM
gcc -Wall -Wextra -Werror -O1 \
    -I"$REPO_ROOT/external/nd-world/include" \
    -I"$REPO_ROOT/external/libchaff/include" \
    -I"$REPO_ROOT/external/libareole/include" \
    -I"$REPO_ROOT/include" \
    -I"$REPO_ROOT/external/axil-nd/include" \
    -I"$REPO_ROOT/external/nd-seven/include" \
    -I"$REPO_ROOT/external/libxylem/include" \
    -I"$REPO_ROOT/external/liblamina/include" \
    -I"$REPO_ROOT/mods/nd/ux" \
    -I"$REPO_ROOT/external/nd-world/include" \
    -I"$REPO_ROOT/external/libchaff/include" \
    -o "$out/test_seven_render" \
    tests/unit/test_seven_render.c \
    mods/nd/ux/nd_seven.c \
    mods/nd/ux/nd_seven_atlas_draw.c \
    mods/nd/ux/nd_seven_ground.c \
    mods/nd/ux/nd_seven_motion.c \
    mods/nd/ux/nd_seven_objects.c \
    mods/nd/ux/nd_seven_gl.c \
    mods/nd/ux/nd_lamina.c \
    external/libareole/src/areole.c \
    -lxxhash -lm
"$out/test_seven_render"
