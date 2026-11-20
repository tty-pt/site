#!/bin/sh
# Standalone unit test runner for nd-plant botanical genetics, MTG phenotype,
# chrono-deterministic simulator, ephemeral delta decay, and 3D geometry builder.
set -eu

CC="${CC:-cc}"
BIN="$(mktemp /tmp/test_nd_plant_model.XXXXXX)"
trap 'rm -f "$BIN"' EXIT

$CC -Wall -Wextra -O2 \
    -Iexternal/nd-plant/include \
    tests/unit/test_nd_plant_model.c \
    -lm -o "$BIN"

"$BIN"
