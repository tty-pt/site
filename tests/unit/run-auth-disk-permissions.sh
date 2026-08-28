#!/bin/sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ROOT/build/test"

clang -Wall -Wextra \
      -I"$ROOT/external/axil-auth/include" \
      -I"$ROOT/external/axil/include" \
      -I"$ROOT/external/libqmap/include" \
      -I"$ROOT/external/libxylem/include" \
      -I"$ROOT/external/bud/include" \
      -I"$ROOT/external/hyle/include" \
      -I"$ROOT/external/hyle/c/libhyle-source/include" \
      -I"$ROOT/external/hyle/c/libhyle-bud/include" \
      -I"$ROOT/external/stoma/include" \
      -I"$ROOT/mods" \
      -L"$ROOT/external/axil-auth/lib" \
      -Wl,-rpath,"$ROOT/external/axil-auth/lib" \
      -L"$ROOT/external/axil/lib" \
      -Wl,-rpath,"$ROOT/external/axil/lib" \
      -L"$ROOT/external/libqmap/lib" \
      -Wl,-rpath,"$ROOT/external/libqmap/lib" \
      -L"$ROOT/external/libxylem/lib" \
      -Wl,-rpath,"$ROOT/external/libxylem/lib" \
      -L"$ROOT/external/bud/lib" \
      -Wl,-rpath,"$ROOT/external/bud/lib" \
      -L"$ROOT/external/hyle/lib" \
      -Wl,-rpath,"$ROOT/external/hyle/lib" \
      -L"$ROOT/external/hyle/c/libhyle-source/lib" \
      -Wl,-rpath,"$ROOT/external/hyle/c/libhyle-source/lib" \
      -L"$ROOT/external/hyle/c/libhyle-bud/lib" \
      -Wl,-rpath,"$ROOT/external/hyle/c/libhyle-bud/lib" \
      -L"$ROOT/external/stoma/lib" \
      -Wl,-rpath,"$ROOT/external/stoma/lib" \
      -laxil-auth -laxil -lqmap -lxylem -lbud -lhyle -lhyle-source -lhyle-bud -lstoma -ljson-c -lqsys \
      "$ROOT/tests/unit/auth_disk_permissions_test.c" \
      -o "$ROOT/build/test/auth_disk_permissions_test"

"$ROOT/build/test/auth_disk_permissions_test"
