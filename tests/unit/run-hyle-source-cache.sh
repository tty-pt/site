#!/bin/sh
set -e
tmpfile=$(mktemp /tmp/hyle_source_cache_test.XXXXXX)
trap 'rm -f "$tmpfile"' EXIT
clang -Wall -Wextra -Werror -I external/libhyle-source/include -I external/libhyle/include -I external/libcorm/include -I external/liblemma/include -L external/libhyle-source/lib -lhyle-source tests/unit/test_hyle_source_cache.c -o "$tmpfile"
LD_LIBRARY_PATH=external/libhyle-source/lib:external/libhyle/lib:external/libcorm/lib:external/liblemma/lib "$tmpfile"
