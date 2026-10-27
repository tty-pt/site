#!/bin/sh
set -eu
REPO_ROOT="$(pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT HUP INT TERM
. "$REPO_ROOT/tests/unit/seven-golden.sh"
gcc -Wall -Wextra -Werror -O1 -DSEVEN_GOLDEN="$SEVEN_GOLDEN" \
    -I"$REPO_ROOT/external/nd-seven/include" \
    -o "$out/test_seven_room" tests/unit/test_seven_room.c
"$out/test_seven_room"
