#!/bin/sh
set -eu
REPO_ROOT="$(pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT HUP INT TERM

gcc -Wall -Wextra -Werror -O1 -ffunction-sections -Wl,--gc-sections \
    -I"$REPO_ROOT/external/nd-world/include" \
    -I"$REPO_ROOT/external/libchaff/include" \
    -I"$REPO_ROOT/external/libislet/include" \
    -I"$REPO_ROOT/external/libareole/include" \
    -I"$REPO_ROOT/include" \
    -I"$REPO_ROOT/external/axil-nd/include" \
    -I"$REPO_ROOT/external/nd-seven/include" \
    -I"$REPO_ROOT/external/nd-plant/include" \
    -I"$REPO_ROOT/external/nd-stone/include" \
    -I"$REPO_ROOT/external/nd-attr/include" \
    -I"$REPO_ROOT/external/libxylem/include" \
    -I"$REPO_ROOT/external/liblamina/include" \
    -I"$REPO_ROOT/mods/nd/ux" \
    -o "$out/test_alt_profile" \
    tests/unit/test_alt_profile.c \
    mods/nd/ux/nd_seven_ground.c \
    -lm

"$out/test_alt_profile"
