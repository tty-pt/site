#!/bin/sh
set -e
tmpfile=$(mktemp /tmp/source_dataset_options_test.XXXXXX)
trap 'rm -f "$tmpfile"' EXIT
clang -Wall -Wextra -Werror -I external/libqmap/include -L external/libqmap/lib -lqmap tests/unit/source_dataset_options_test.c -o "$tmpfile"
LD_LIBRARY_PATH=external/libqmap/lib "$tmpfile"
