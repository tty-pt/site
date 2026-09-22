#!/bin/sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ROOT/build/test"

clang -I"$ROOT/external/libhyle-source/include" \
      -I"$ROOT/external/libhyle/include" \
      -I"$ROOT/external/libqmap/include" \
      -L"$ROOT/external/libhyle-source/lib" \
      -Wl,-rpath,"$ROOT/external/libhyle-source/lib" \
      -L"$ROOT/external/libhyle/lib" \
      -Wl,-rpath,"$ROOT/external/libhyle/lib" \
      -L"$ROOT/external/libqmap/lib" \
      -Wl,-rpath,"$ROOT/external/libqmap/lib" \
      -lhyle-source -lhyle -lqmap -ljson-c \
      "$ROOT/tests/unit/source_utils_test.c" \
      -o "$ROOT/build/test/source_utils_test"

"$ROOT/build/test/source_utils_test"
