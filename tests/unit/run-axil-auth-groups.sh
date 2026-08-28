#!/bin/sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ROOT/build/test"

clang -Wall -Wextra \
      -I"$ROOT/external/axil-auth/include" \
      -I"$ROOT/external/axil/include" \
      -I"$ROOT/external/libqmap/include" \
      -I"$ROOT/external/libxylem/include" \
      -L"$ROOT/external/axil-auth/lib" \
      -Wl,-rpath,"$ROOT/external/axil-auth/lib" \
      -L"$ROOT/external/axil/lib" \
      -Wl,-rpath,"$ROOT/external/axil/lib" \
      -L"$ROOT/external/libqmap/lib" \
      -Wl,-rpath,"$ROOT/external/libqmap/lib" \
      -L"$ROOT/external/libxylem/lib" \
      -Wl,-rpath,"$ROOT/external/libxylem/lib" \
      -laxil-auth -laxil -lqmap -lxylem -lqsys \
      "$ROOT/tests/unit/axil_auth_groups_test.c" \
      -o "$ROOT/build/test/axil_auth_groups_test"

"$ROOT/build/test/axil_auth_groups_test"
