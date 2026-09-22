#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
bin=$tmpdir/site_media_test

${CC:-clang} -Wall -Wextra -Werror \
	-I"$repo/external/libbud/include" \
	-I"$repo/external/libhyle/include" \
	-I"$repo/external/libhyle-bud/include" \
	-I"$repo/mods/common" \
	-I"$repo/mods/common/ux" \
	-o "$bin" \
	"$repo/tests/unit/site_media_test.c" \
	-L"$repo/external/libbud/lib" -lbud \
	-Wl,-rpath,"$repo/external/libbud/lib"

"$bin"
