#!/bin/sh
set -eu

REPO_ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT HUP INT TERM

echo "=== Testing i18n unit tests ==="
clang -Wall -Wextra -Werror -o "$TMPDIR/i18n_test" "$REPO_ROOT/tests/unit/i18n_test.c"
"$TMPDIR/i18n_test"

clang -Wall -Wextra -Werror -o "$TMPDIR/i18n_locale_test" "$REPO_ROOT/tests/unit/i18n_locale_test.c"
"$TMPDIR/i18n_locale_test"

echo "i18n tests: ALL PASS"
