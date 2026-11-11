#!/bin/sh
set -eu
REPO_ROOT="$(pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT HUP INT TERM
gcc -Wall -Wextra -Werror -O1 \
    -I"$REPO_ROOT/mods/nd/ux" \
    -I"$REPO_ROOT/external/nd-world/include" \
    -I"$REPO_ROOT/external/libchaff/include" \
    -I"$REPO_ROOT/external/libareole/include" \
    -I"$REPO_ROOT/include" \
    -I"$REPO_ROOT/external/axil-nd/include" \
    -I"$REPO_ROOT/external/nd-seven/include" \
    -I"$REPO_ROOT/external/libxylem/include" \
    -I"$REPO_ROOT/external/liblamina/include" \
    -o "$out/test_torus_planetary_renderer" \
    tests/unit/test_torus_planetary_renderer.c \
    mods/nd/ux/nd_seven.c \
    mods/nd/ux/nd_seven_atlas_draw.c \
    mods/nd/ux/nd_seven_ground.c \
    mods/nd/ux/nd_seven_motion.c \
    mods/nd/ux/nd_seven_objects.c \
    mods/nd/ux/nd_lamina.c \
    external/libareole/src/areole.c \
    -lxxhash -lm
"$out/test_torus_planetary_renderer"

gcc -Wall -Wextra -Werror -O1 \
    -I"$REPO_ROOT/external/nd-world/include" \
    -I"$REPO_ROOT/external/libchaff/include" \
    -I"$REPO_ROOT/external/libxylem/include" \
    -o "$out/test_planetary_continents" \
    tests/unit/test_planetary_continents.c \
    -lxxhash -lm
"$out/test_planetary_continents"
