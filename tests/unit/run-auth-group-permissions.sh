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
      -I"$ROOT/external/libbud/include" \
      -I"$ROOT/external/libhyle/include" \
      -I"$ROOT/external/libhyle-source/include" \
      -I"$ROOT/external/libhyle-bud/include" \
      -I"$ROOT/external/liblemma/include" \
      -L"$ROOT/external/axil/lib" \
      -L"$ROOT/external/axil-auth/lib" \
      -L"$ROOT/external/libcorm/lib" \
      -L"$ROOT/external/libxylem/lib" \
      -L"$ROOT/external/libstele/lib" \
      -L"$ROOT/external/liblemma/lib" \
      -I"$ROOT/mods" \
      -laxil -lcorm -lxylem -lbud -lhyle -lhyle-source -lhyle-bud -llemma -ljson-c -lstele \
      "$ROOT/tests/unit/auth_group_permissions_test.c" \
      -o "$ROOT/build/test/auth_group_permissions_test"

LD_LIBRARY_PATH="$ROOT/external/axil/lib:$ROOT/external/axil-auth/lib:$ROOT/external/libstele/lib:$ROOT/external/libcorm/lib:$ROOT/external/libxylem/lib:$ROOT/external/liblemma/lib:${LD_LIBRARY_PATH:-/usr/lib}" "$ROOT/build/test/auth_group_permissions_test"
