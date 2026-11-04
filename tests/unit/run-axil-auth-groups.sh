#!/bin/sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ROOT/build/test"

clang -Wall -Wextra \
      -I"$ROOT/external/axil-auth/include" \
      -I"$ROOT/external/axil/include" \
      -I"$ROOT/external/libstele/include" \
      -I"$ROOT/external/libcorm/include" \
      -I"$ROOT/external/libxylem/include" \
      -L"$ROOT/external/axil-auth/lib" \
      -L"$ROOT/external/axil/lib" \
      -L"$ROOT/external/libcorm/lib" \
      -L"$ROOT/external/libxylem/lib" \
      -L"$ROOT/external/libstele/lib" \
      -laxil-auth -laxil -lcorm -lxylem -lstele \
      "$ROOT/tests/unit/axil_auth_groups_test.c" \
      -o "$ROOT/build/test/axil_auth_groups_test"

LD_LIBRARY_PATH="$ROOT/external/axil-auth/lib:$ROOT/external/axil/lib:$ROOT/external/libcorm/lib:$ROOT/external/libxylem/lib:$ROOT/external/libstele/lib:${LD_LIBRARY_PATH:-/usr/lib}" "$ROOT/build/test/axil_auth_groups_test"
