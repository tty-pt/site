#!/bin/sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ROOT/build/test"

clang -Wall -Wextra \
      -I"$ROOT/external/axil-auth/include" \
      -I"$ROOT/external/axil/include" \
      -I"$ROOT/external/libqsys/include" \
      -I"$ROOT/external/libcorm/include" \
      -I"$ROOT/external/libxylem/include" \
      -I"$ROOT/external/libbud/include" \
      -I"$ROOT/external/libhyle/include" \
      -I"$ROOT/external/libhyle-source/include" \
      -I"$ROOT/external/libhyle-bud/include" \
      -I"$ROOT/external/liblemma/include" \
      -L"$ROOT/external/liblemma/lib" \
      -Wl,-rpath,"$ROOT/external/liblemma/lib" \
      -I"$ROOT/mods" \
      -laxil -lcorm -lxylem -lbud -lhyle -lhyle-source -lhyle-bud -llemma -ljson-c -lqsys \
      "$ROOT/tests/unit/auth_group_permissions_test.c" \
      -o "$ROOT/build/test/auth_group_permissions_test"

LD_LIBRARY_PATH="$ROOT/external/axil-auth/lib:${LD_LIBRARY_PATH:-/usr/lib}" "$ROOT/build/test/auth_group_permissions_test"
