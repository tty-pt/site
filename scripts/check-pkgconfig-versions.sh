#!/bin/sh
set -eu

# check-pkgconfig-versions.sh
# Verifies that pkg-config *.pc files match their respective package.json versions across the monorepo.

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
echo "=== Pkg-config Version Parity Check ==="

errors=0

for dir in "$root"/external/lib*; do
    [ -d "$dir" ] || continue
    name=$(basename "$dir" | sed 's/^lib//')

    pkg_json="$dir/npm/package.json"
    [ -f "$pkg_json" ] || continue

    expected_ver=$(node -p "require('$pkg_json').version" 2>/dev/null || true)
    [ -n "$expected_ver" ] || continue

    for pc in "$dir"/*.pc "$dir"/npm/*.pc; do
        [ -f "$pc" ] || continue
        pc_ver=$(grep -i '^Version:' "$pc" | head -n 1 | awk '{print $2}')
        rel_pc=$(echo "$pc" | sed "s|^$root/||")
        if [ "$pc_ver" != "$expected_ver" ]; then
            echo "FAIL: $rel_pc version ($pc_ver) does not match package.json ($expected_ver)" >&2
            errors=$((errors + 1))
        else
            echo "PASS: $rel_pc matches $expected_ver"
        fi
    done
done

if [ "$errors" -gt 0 ]; then
    echo "ERROR: $errors pkg-config version mismatch(es) detected!" >&2
    exit 1
fi

echo "OK: all pkg-config file versions match package.json"
exit 0
