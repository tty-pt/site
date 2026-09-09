#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
bin=$tmpdir/index_helpers_test

${CC:-clang} -Wall -Wextra -Werror \
	-I"$repo/external/hyle/include" \
	-I"$repo/external/hyle/c/libhyle-source/include" \
	-I"$repo/external/hyle/c/libhyle-bud/include" \
	-I"$repo/external/bud/include" \
	-I"$repo/external/axil/include" \
	-I"$repo/external/axil-auth/include" \
	-I"$repo/external/libqmap/include" \
	-I"$repo/external/libxylem/include" \
	-I"$repo/mods/common" \
	-I"$repo" \
	-o "$bin" \
	"$repo/tests/unit/index_helpers_test.c" \
	-L"$repo/mods/index" -Wl,-rpath,"$repo/mods/index" -l:index.so \
	-L"$repo/mods/common" -Wl,-rpath,"$repo/mods/common" -l:common.so \
	-L"$repo/external/hyle/c/libhyle-bud/lib" -lhyle-bud \
	-L"$repo/external/hyle/c/libhyle-source/lib" -lhyle-source \
	-L"$repo/external/hyle/lib" -lhyle \
	-L"$repo/external/bud/lib" -lbud \
	-L"$repo/external/libqmap/lib" -lqmap \
	-L"$repo/external/axil/lib" -laxil \
	-L"$repo/external/axil-auth/lib" -laxil-auth \
	-Wl,-rpath,"$repo/external/hyle/c/libhyle-bud/lib" \
	-Wl,-rpath,"$repo/external/hyle/c/libhyle-source/lib" \
	-Wl,-rpath,"$repo/external/hyle/lib" \
	-Wl,-rpath,"$repo/external/bud/lib" \
	-Wl,-rpath,"$repo/external/libqmap/lib" \
	-Wl,-rpath,"$repo/external/axil/lib" \
	-Wl,-rpath,"$repo/external/axil-auth/lib"

"$bin"
