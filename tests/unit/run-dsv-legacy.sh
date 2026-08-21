#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
bin=$tmpdir/dsv_legacy_test

${CC:-cc} ${CFLAGS:-} -std=gnu11 -Wall -Wextra -Werror \
	-o "$bin" "$repo/tests/unit/dsv_legacy_test.c"
"$bin"
