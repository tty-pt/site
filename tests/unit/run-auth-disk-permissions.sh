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
      -L"$ROOT/external/axil-auth/lib" \
      -Wl,-rpath,"$ROOT/external/axil-auth/lib" \
      -L"$ROOT/external/axil/lib" \
      -Wl,-rpath,"$ROOT/external/axil/lib" \
      -L"$ROOT/external/libqsys/lib" \
      -Wl,-rpath,"$ROOT/external/libqsys/lib" \
      -L"$ROOT/external/libcorm/lib" \
      -Wl,-rpath,"$ROOT/external/libcorm/lib" \
      -L"$ROOT/external/libxylem/lib" \
      -Wl,-rpath,"$ROOT/external/libxylem/lib" \
      -L"$ROOT/external/libbud/lib" \
      -Wl,-rpath,"$ROOT/external/libbud/lib" \
      -L"$ROOT/external/libhyle/lib" \
      -Wl,-rpath,"$ROOT/external/libhyle/lib" \
      -L"$ROOT/external/libhyle-source/lib" \
      -Wl,-rpath,"$ROOT/external/libhyle-source/lib" \
      -L"$ROOT/external/libhyle-bud/lib" \
      -Wl,-rpath,"$ROOT/external/libhyle-bud/lib" \
      -L"$ROOT/external/libstoma/lib" \
      -Wl,-rpath,"$ROOT/external/libstoma/lib" \
      -laxil-auth -laxil -lcorm -lxylem -lbud -lhyle -lhyle-source -lhyle-bud -lstoma -ljson-c -lqsys \
      "$ROOT/tests/unit/auth_disk_permissions_test.c" \
      -o "$ROOT/build/test/auth_disk_permissions_test"

"$ROOT/build/test/auth_disk_permissions_test"
