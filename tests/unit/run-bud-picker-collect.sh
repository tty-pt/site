#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
bin=$tmpdir/bud_picker_collect_test

${CC:-clang} -Wall -Wextra -Werror \
	-I"$repo/external/libhyle/include" \
	-I"$repo/external/libhyle-source/include" \
	-I"$repo/external/libhyle-bud/include" \
	-I"$repo/external/libbud/include" \
	-I"$repo/external/libcorm/include" \
	-I"$repo/external/liblemma/include" \
	-L"$repo/external/liblemma/lib" \
	-L"$repo/external/libhyle/lib" \
	-L"$repo/external/libhyle-source/lib" \
	-L"$repo/external/libhyle-bud/lib" \
	-L"$repo/external/libbud/lib" \
	-Wl,-rpath,"$repo/external/liblemma/lib" \
	-Wl,-rpath,"$repo/external/libhyle/lib" \
	-Wl,-rpath,"$repo/external/libhyle-source/lib" \
	-Wl,-rpath,"$repo/external/libhyle-bud/lib" \
	-Wl,-rpath,"$repo/external/libbud/lib" \
	-o "$bin" \
	"$repo/tests/unit/bud_picker_collect_test.c" \
	-lhyle-bud \
	-lhyle-source \
	-lhyle \
	-lbud \
	-lcorm \
	-llemma \
	-ljson-c \

"$bin"
