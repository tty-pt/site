#!/bin/sh
set -e
tmpfile=$(mktemp /tmp/ordered_sync_test.XXXXXX)
trap 'rm -f "$tmpfile"' EXIT
clang -Wall -Wextra -Werror tests/unit/ordered_sync_test.c -o "$tmpfile"
"$tmpfile"
