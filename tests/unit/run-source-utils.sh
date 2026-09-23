#!/bin/sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ROOT/build/test"

clang -I"$ROOT/external/libhyle-source/include" \
      -I"$ROOT/external/libhyle/include" \
      -I"$ROOT/external/libcorm/include" \
      -L"$ROOT/external/libhyle-source/lib" \
      -Wl,-rpath,"$ROOT/external/libhyle-source/lib" \
      -L"$ROOT/external/libhyle/lib" \
      -Wl,-rpath,"$ROOT/external/libhyle/lib" \
      -L"$ROOT/external/libcorm/lib" \
      -Wl,-rpath,"$ROOT/external/libcorm/lib" \
      -lhyle-source -lhyle -lcorm -ljson-c \
      "$ROOT/tests/unit/source_utils_test.c" \
      -o "$ROOT/build/test/source_utils_test"

"$ROOT/build/test/source_utils_test"
