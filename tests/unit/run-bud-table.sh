#!/bin/sh
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$DIR/../.."

gcc -Wall -Wextra -Werror -I"$ROOT/external/bud/include" -I"$ROOT/external/hyle/c/libhyle-bud/include" \
    -I"$ROOT/external/hyle/c/libhyle" -I"$ROOT/external/hyle/c/libhyle-source/include" \
    "$DIR/bud_table_test.c" \
    "$ROOT/external/bud/src/libbud.c" \
    "$ROOT/external/hyle/c/libhyle-bud/src/table.c" \
    -o "$DIR/bud_table_test"

"$DIR/bud_table_test"
rm -f "$DIR/bud_table_test"
