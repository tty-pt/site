#!/bin/sh
set -eu
REPO_ROOT="$(pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT HUP INT TERM
gcc -Wall -Wextra -Werror -O1 \
    -I"$REPO_ROOT/external/libislet/include" \
    -I"$REPO_ROOT/external/libareole/include" \
    -I"$REPO_ROOT/external/libchaff/include" \
    -I"$REPO_ROOT/external/libcorolla/include" \
    -I"$REPO_ROOT/include" \
    -o "$out/test_areole_chaff_occlusion" \
    tests/unit/test_areole_chaff_occlusion.c \
    external/libareole/src/areole.c \
    -lxxhash -lm
"$out/test_areole_chaff_occlusion"
