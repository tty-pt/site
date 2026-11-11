#!/bin/sh
set -eu
REPO_ROOT="$(pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT HUP INT TERM

gcc -Wall -Wextra -O2 \
    -I"$REPO_ROOT/external/nd-world/include" \
    -I"$REPO_ROOT/external/libchaff/include" \
    -I"$REPO_ROOT/external/libareole/include" \
    -I"$REPO_ROOT/include" \
    -I"$REPO_ROOT/external/axil-nd/include" \
    -I"$REPO_ROOT/external/nd-seven/include" \
    -I"$REPO_ROOT/external/libxylem/include" \
    -I"$REPO_ROOT/external/liblamina/include" \
    -I"$REPO_ROOT/mods/nd/ux" \
    -o "$out/bench_field" \
    tests/unit/bench_field.c \
    mods/nd/ux/nd_seven_ground.c \
    mods/nd/ux/nd_seven_motion.c \
    mods/nd/ux/nd_seven_objects.c \
    mods/nd/ux/nd_seven_atlas_draw.c \
    mods/nd/ux/nd_lamina.c \
    -lxxhash

"$out/bench_field"
