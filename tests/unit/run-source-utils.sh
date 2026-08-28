#!/bin/sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ROOT/build/test"

clang -I"$ROOT/external/hyle/c/libhyle-source/include" \
      -I"$ROOT/external/hyle/include" \
      -I"$ROOT/external/libqmap/include" \
      -L"$ROOT/external/hyle/c/libhyle-source/lib" \
      -Wl,-rpath,"$ROOT/external/hyle/c/libhyle-source/lib" \
      -L"$ROOT/external/hyle/lib" \
      -Wl,-rpath,"$ROOT/external/hyle/lib" \
      -L"$ROOT/external/libqmap/lib" \
      -Wl,-rpath,"$ROOT/external/libqmap/lib" \
      -lhyle-source -lhyle -lqmap -ljson-c \
      "$ROOT/tests/unit/source_utils_test.c" \
      -o "$ROOT/build/test/source_utils_test"

"$ROOT/build/test/source_utils_test"
