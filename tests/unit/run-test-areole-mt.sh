#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${TMPDIR:-/tmp}/test_areole_mt_$$"
trap 'rm -f "$BIN"' EXIT

cc -O2 -g -Wall -Wextra -Wno-unused-function -pthread \
	-I"$ROOT/external/libstele/include" \
	-I"$ROOT/external/libareole/include" \
	-I"$ROOT/external/libislet/include" \
	-I"$ROOT/external/libchaff/include" \
	-I"$ROOT/external/libcorolla/include" \
	-I"$ROOT/external/nd-world/include" \
	"$ROOT/tests/unit/test_areole_mt.c" \
	"$ROOT/external/libcorolla/src/corolla.c" \
	-L"$ROOT/external/libstele/lib" -Wl,-rpath,"$ROOT/external/libstele/lib" \
	-L"$ROOT/external/libareole/lib" -Wl,-rpath,"$ROOT/external/libareole/lib" \
	-lareole -lstele -lm -pthread \
	-o "$BIN"

"$BIN"
