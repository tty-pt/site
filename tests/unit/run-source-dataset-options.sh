#!/bin/sh
set -e
tmpfile=$(mktemp /tmp/source_dataset_options_test.XXXXXX)
trap 'rm -f "$tmpfile"' EXIT
clang -Wall -Wextra -Werror -I external/libcorm/include -lcorm tests/unit/source_dataset_options_test.c -o "$tmpfile"
"$tmpfile"
