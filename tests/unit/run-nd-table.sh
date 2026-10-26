#!/bin/sh
set -e
ROOT="$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)"
BIN="${TMPDIR:-/tmp}/nd_table_test_$$"
cleanup() { rm -f "$BIN"; }
trap cleanup EXIT INT TERM
cd "$ROOT"
clang -Wall -Wextra -I"$ROOT/external/axil-nd/include" -I"$ROOT/external/libxylem/include" -I"$ROOT/external/libqsys/include" \
	tests/unit/nd_table_test.c external/axil-nd/src/law_data.c -o "$BIN" -lxylem -lqsys
"$BIN"
