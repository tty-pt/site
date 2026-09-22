#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
bin=$tmpdir/bud_form_test

${CC:-clang} -Wall -Wextra -Werror \
	-I"$repo/external/libhyle/include" \
	-I"$repo/external/libhyle-source/include" \
	-I"$repo/external/libhyle-bud/include" \
	-I"$repo/external/libbud/include" \
	-I"$repo/external/libqmap/include" \
	-I"$repo/external/libstoma/include" \
	-o "$bin" \
	"$repo/tests/unit/bud_form_test.c" \
	-L"$repo/external/libhyle-bud/lib" -lhyle-bud \
	-L"$repo/external/libhyle-source/lib" -lhyle-source \
	-L"$repo/external/libhyle/lib" -lhyle \
	-L"$repo/external/libbud/lib" -lbud \
	-L"$repo/external/libqmap/lib" -lqmap \
	-L"$repo/external/libstoma/lib" -lstoma \
	-ljson-c \
	-Wl,-rpath,"$repo/external/libhyle-bud/lib" \
	-Wl,-rpath,"$repo/external/libhyle-source/lib" \
	-Wl,-rpath,"$repo/external/libhyle/lib" \
	-Wl,-rpath,"$repo/external/libbud/lib" \
	-Wl,-rpath,"$repo/external/libqmap/lib" \
	-Wl,-rpath,"$repo/external/libstoma/lib"

"$bin"
