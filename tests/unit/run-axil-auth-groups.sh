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
      -laxil-auth -laxil -lcorm -lxylem -lqsys \
      "$ROOT/tests/unit/axil_auth_groups_test.c" \
      -o "$ROOT/build/test/axil_auth_groups_test"

"$ROOT/build/test/axil_auth_groups_test"
