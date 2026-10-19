#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
bin=$tmpdir/externals_abstractions_test

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
	-I"$repo/mods/source" \
	-I"$repo" \
	-o "$bin" \
	"$repo/tests/unit/externals_abstractions_test.c" \
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
