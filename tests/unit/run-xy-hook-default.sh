#!/bin/sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ROOT/build/test"

clang -Wall -Wextra \
      -I"$ROOT/external/libxylem/include" \
      -L"$ROOT/external/libxylem/lib" \
      -Wl,-rpath,"$ROOT/external/libxylem/lib" \
      -lxylem \
      "$ROOT/tests/unit/xy_hook_default_test.c" \
      -o "$ROOT/build/test/xy_hook_default_test"

"$ROOT/build/test/xy_hook_default_test"