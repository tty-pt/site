#!/bin/sh
set -eu

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$REPO_ROOT/tests/unit/test_hyle_table_bin"

gcc -Wall -Wextra -Werror -O2 \
    -I"$REPO_ROOT" \
    -I"$REPO_ROOT/mods/common/ux" \
    -o "$BIN" \
    "$REPO_ROOT/tests/unit/test_hyle_table_bin.c"

"$BIN"
rm -f "$BIN"
