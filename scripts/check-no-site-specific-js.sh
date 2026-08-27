#!/bin/sh
set -eu

# check-no-site-specific-js.sh
#
# Enforces the architectural invariant that htdocs/*.js contains ZERO site-specific
# identifiers, domain modules, or selectors (gig, song, poem, grp, repertoire, transpose, sb-, chord).
# JavaScript must remain strictly generic library infrastructure (hyle / bud).

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

forbidden_pattern='gig|song|poem|grp|repertoire|transpose|sb-|chord'

echo "=== JavaScript site-specificity check ==="

violations=0
for js in "$root"/htdocs/*.js; do
    [ -f "$js" ] || continue
    matches=$(grep -inE "$forbidden_pattern" "$js" || true)
    if [ -n "$matches" ]; then
        echo "FAIL: Site-specific code detected in $(basename "$js"):" >&2
        echo "$matches" >&2
        violations=$((violations + 1))
    fi
done

if [ "$violations" -gt 0 ]; then
    echo "ERROR: $violations JS file(s) contain site-specific logic or domain terms." >&2
    exit 1
fi

echo "OK: no site-specific JavaScript detected in htdocs/*.js"
exit 0
