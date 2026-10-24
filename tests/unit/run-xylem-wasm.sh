#!/bin/sh
set -e
ROOT="$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)"
BIN="${TMPDIR:-/tmp}/xylem_wasm_test_$$"
cleanup() { rm -f "$BIN"; }
trap cleanup EXIT INT TERM
clang -Wall -Wextra -Werror \
	-I"$ROOT" \
	-I"$ROOT/external/libxylem/include" \
	"$ROOT/tests/unit/xylem_wasm_test.c" "$ROOT/external/libxylem/src/libxylem-wasm.c" \
	-o "$BIN"
"$BIN"
