#!/bin/sh
set -e
ROOT="$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)"
BIN="${TMPDIR:-/tmp}/nd_bcp_test_$$"
cleanup() { rm -f "$BIN"; }
trap cleanup EXIT INT TERM
clang -Wall -Wextra -I"$ROOT" -I"$ROOT/external/axil-nd/include" -I"$ROOT/external/libxylem/include" -I"$ROOT/external/libqsys/include" \
	"$ROOT/tests/unit/nd_bcp_test.c" "$ROOT/tests/unit/nd_bcp_mock_equip.c" "$ROOT/external/libxylem/src/libxylem-wasm.c" \
	-L"$ROOT/external/libqsys/lib" -L/usr/lib -lqsys -o "$BIN"
"$BIN"
