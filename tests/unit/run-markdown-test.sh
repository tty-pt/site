#!/bin/sh
set -eu

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TMPFILE=$(mktemp /tmp/test-markdown-XXXXXX)
trap 'rm -f "$TMPFILE"' EXIT INT TERM

echo "=== Testing Markdown parser ==="
clang -Wall -Wextra -Werror -I"$REPO_ROOT" \
	"$REPO_ROOT/mods/software/markdown.c" \
	"$REPO_ROOT/tests/unit/test-markdown.c" \
	-o "$TMPFILE"
"$TMPFILE"
echo "Markdown parser tests: ALL PASS"
