#!/bin/sh
set -eu
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT HUP INT TERM
gcc -Wall -Wextra -Werror -Iexternal/nd-stone/include -o "$out/test_stone_roll" tests/unit/test_stone_roll.c
"$out/test_stone_roll"
