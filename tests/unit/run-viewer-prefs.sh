#!/bin/sh
set -e
tmpfile=$(mktemp /tmp/viewer_prefs_test.XXXXXX)
trap 'rm -f "$tmpfile"' EXIT
clang -Wall -Wextra -Werror -I mods tests/unit/viewer_prefs_test.c -o "$tmpfile"
"$tmpfile"
