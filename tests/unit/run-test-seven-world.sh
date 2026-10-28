#!/bin/sh
set -eu
REPO_ROOT="$(pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT HUP INT TERM
gcc -Wall -Wextra -Werror -O1 \
    -I"$REPO_ROOT/external/nd-seven/include" \
    -o "$out/test_seven_world" tests/unit/test_seven_world.c
"$out/test_seven_world"
