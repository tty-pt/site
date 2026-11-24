#!/bin/sh
set -eu
# check-asset-sync.sh — Ensure staged htdocs/ files match their canonical
# sources in external/*/htdocs/ and mods/*/htdocs/ with zero silent drift.

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"

FAIL=0
for d in external/* mods/*; do
	[ -d "$d/htdocs" ] || continue
	for src in $(find "$d/htdocs" -type f | sort); do
		rel="${src#$d/htdocs/}"
		dst="htdocs/$rel"
		if [ ! -f "$dst" ]; then
			echo "FAIL: staged asset missing: $dst (source: $src)" >&2
			FAIL=1
		elif ! cmp -s "$src" "$dst"; then
			echo "FAIL: staged asset drift: $dst differs from $src (edit $src and re-run make stage-assets)" >&2
			FAIL=1
		fi
	done
done

if [ "$FAIL" -ne 0 ]; then
	echo "asset-sync check FAILED" >&2
	exit 1
fi
echo "asset-sync check PASS: all staged htdocs assets match canonical sources"
