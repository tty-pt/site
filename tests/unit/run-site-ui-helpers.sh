#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
bin=$tmpdir/site_ui_helpers_test

${CC:-clang} -Wall -Wextra -Werror \
	-I"$repo/external/libhyle/include" \
	-I"$repo/external/libhyle-source/include" \
	-I"$repo/external/libhyle-bud/include" \
	-I"$repo/external/libbud/include" \
	-I"$repo/external/axil/include" \
	-I"$repo/external/libcorm/include" \
	-I"$repo" \
	-o "$bin" \
	"$repo/tests/unit/site_ui_helpers_test.c" \
	-L"$repo/mods/common" -Wl,-rpath,"$repo/mods/common" -l:common.so \
	-lhyle-bud \
	-lbud \
	-lxylem \

"$bin"
