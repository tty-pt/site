#!/bin/sh
set -e
ROOT="$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)"
BIN="${TMPDIR:-/tmp}/nd_ext_state_test_$$"
cleanup() { rm -f "$BIN"; }
trap cleanup EXIT INT TERM
clang -Wall -Wextra -I"$ROOT" -I"$ROOT/external/axil-nd/include" -I"$ROOT/external/nd-spell/include" -I"$ROOT/external/nd-equip/include" -I"$ROOT/external/nd-mortal/include" \
	-I"$ROOT/external/libxylem/include" -I"$ROOT/external/libqsys/include" \
	"$ROOT/tests/unit/nd_ext_state_test.c" -o "$BIN"
"$BIN"
