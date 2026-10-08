#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
bin=$tmpdir/index_helpers_test

${CC:-clang} -Wall -Wextra -Werror \
	-I"$repo/external/libhyle/include" \
	-I"$repo/external/libhyle-source/include" \
	-I"$repo/external/libhyle-bud/include" \
	-I"$repo/external/libbud/include" \
	-I"$repo/external/axil/include" \
	-I"$repo/external/axil-auth/include" \
	-I"$repo/external/libqsys/include" \
	-I"$repo/external/libcorm/include" \
	-I"$repo/external/libxylem/include" \
	-I"$repo/mods/common" \
	-I"$repo" \
	-o "$bin" \
	"$repo/tests/unit/index_helpers_test.c" \
	-L"$repo/mods/index" -Wl,-rpath,"$repo/mods/index" -l:index.so \
	-L"$repo/mods/common" -Wl,-rpath,"$repo/mods/common" -l:common.so \
	-lhyle-bud \
	-lhyle-source \
	-lhyle \
	-lbud \
	-lqsys \
	-lcorm \
	-laxil \
	-laxil-auth \
	-lxylem \

"$bin"
