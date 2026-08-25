#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
bin=$tmpdir/mpfd_multivalue_test

${CC:-cc} ${CFLAGS:-} -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function \
	-I"$repo/external/axil/include" \
	-I"$repo/external/libqmap/include" \
	-I"$repo/external/libxylem/include" \
	-o "$bin" "$repo/tests/unit/mpfd_multivalue_test.c" \
	-L"$repo/external/libqmap/lib" -lqmap
LD_LIBRARY_PATH="$repo/external/libqmap/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$bin"
