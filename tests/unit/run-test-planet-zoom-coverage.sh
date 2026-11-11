#!/bin/sh
set -eu
REPO_ROOT="$(pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT HUP INT TERM

gcc -Wall -Wextra -Werror -O2 \
    -I"$REPO_ROOT/external/libareole/include" \
    -I"$REPO_ROOT/external/libislet/include" \
    -I"$REPO_ROOT/external/libchaff/include" \
    -o "$out/test_planet_zoom_coverage" \
    "$REPO_ROOT/external/libareole/src/areole.c" \
    "$REPO_ROOT/tests/unit/test_planet_zoom_coverage.c" \
    -lm -lxxhash

"$out/test_planet_zoom_coverage"
