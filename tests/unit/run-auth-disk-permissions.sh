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
      -I"$ROOT/external/libstoma/include" \
      -I"$ROOT/mods" \
      -laxil-auth -laxil -lcorm -lxylem -lbud -lhyle -lhyle-source -lhyle-bud -lstoma -ljson-c -lqsys \
      "$ROOT/tests/unit/auth_disk_permissions_test.c" \
      -o "$ROOT/build/test/auth_disk_permissions_test"

"$ROOT/build/test/auth_disk_permissions_test"
