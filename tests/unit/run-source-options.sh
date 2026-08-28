#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
bin=$tmpdir/source_options_test

${CC:-clang} -Wall -Wextra -Werror \
	-I"$repo/external/hyle/include" \
	-I"$repo/external/hyle/c/libhyle-source/include" \
	-I"$repo/external/libqmap/include" \
	-I"$repo/external/stoma/include" \
	-o "$bin" \
	"$repo/tests/unit/source_options_test.c" \
	-L"$repo/external/hyle/c/libhyle-source/lib" -lhyle-source \
	-L"$repo/external/hyle/lib" -lhyle \
	-L"$repo/external/libqmap/lib" -lqmap \
	-L"$repo/external/stoma/lib" -lstoma \
	-ljson-c \
	-Wl,-rpath,"$repo/external/hyle/c/libhyle-source/lib" \
	-Wl,-rpath,"$repo/external/hyle/lib" \
	-Wl,-rpath,"$repo/external/libqmap/lib" \
	-Wl,-rpath,"$repo/external/stoma/lib"

"$bin"
