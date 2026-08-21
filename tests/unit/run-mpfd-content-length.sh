#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
bin=$tmpdir/mpfd_content_length_test

${CC:-cc} ${CFLAGS:-} -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function \
	-I"$repo/external/axil/include" \
	-I"$repo/external/libqmap/include" \
	-I"$repo/external/libxylem/include" \
	-o "$bin" "$repo/tests/unit/mpfd_content_length_test.c"
"$bin"
