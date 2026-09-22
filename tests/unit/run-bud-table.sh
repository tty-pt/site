#!/bin/sh
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$DIR/../.."

${CC:-clang} -Wall -Wextra -Werror -I"$ROOT/external/libbud/include" -I"$ROOT/external/libhyle-bud/include" \
    -I"$ROOT/external/libhyle/include" -I"$ROOT/external/libhyle-source/include" \
    "$DIR/bud_table_test.c" \
    "$ROOT/external/libbud/src/libbud.c" \
    "$ROOT/external/libhyle-bud/src/libhyle-bud.c" \
    "$ROOT/external/libhyle-bud/src/table.c" \
    -ljson-c \
    -o "$DIR/bud_table_test"

"$DIR/bud_table_test"
rm -f "$DIR/bud_table_test"
