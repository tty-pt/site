#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$ROOT/tests/unit/test_map_noise"

gcc -O2 -Wall -DND_WORLD_PROVIDER \
    -DWORLDGEN_GOLDEN="\"$ROOT/tests/unit/data/worldgen_golden.txt\"" \
    -I"$ROOT/external/libnoise/include" \
    -I"$ROOT/external/nd-world/include" \
    -I"$ROOT/external/nd-world/src" \
    -I"$ROOT/external/axil-nd/include" \
    -I"$ROOT/external/libcorm/include" \
    -I"$ROOT/external/nd-region/include" \
    -I"$ROOT/external/nd-session/include" \
    -I"$ROOT/external/nd-view/include" \
    -I"$ROOT/external/nd-obj/include" \
    -I"$ROOT/external/nd-speech/include" \
    -I"$ROOT/external/nd-mcp/include" \
    -I/usr/include \
    "$ROOT/tests/unit/test_map_noise.c" \
    "$ROOT/external/nd-world/src/noise.c" \
    -L"$ROOT/external/libcorm/lib" \
    -L"$ROOT/external/libstele/lib" \
    -lcorm -lstele -lxxhash \
    -o "$BIN"

"$BIN"
rm -f "$BIN"
