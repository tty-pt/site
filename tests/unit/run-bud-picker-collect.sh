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
	-I"$repo/external/libstoma/include" \
	-o "$bin" \
	"$repo/tests/unit/bud_picker_collect_test.c" \
	-lhyle-bud \
	-lhyle-source \
	-lhyle \
	-lbud \
	-lcorm \
	-lstoma \
	-ljson-c \

"$bin"
