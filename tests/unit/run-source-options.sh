#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
bin=$tmpdir/source_options_test

${CC:-clang} -Wall -Wextra -Werror \
	-I"$repo/external/libhyle/include" \
	-I"$repo/external/libhyle-source/include" \
	-I"$repo/external/libcorm/include" \
	-I"$repo/external/libstoma/include" \
	-o "$bin" \
	"$repo/tests/unit/source_options_test.c" \
	-lhyle-source \
	-lhyle \
	-lcorm \
	-lstoma \
	-ljson-c \

"$bin"
