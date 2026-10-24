#!/bin/sh
set -eu

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
failed=0

echo "=== Checking for forbidden *_IMPL guards ==="

# Pass 1a: Check for #ifndef *_IMPL in any *.h across the repo
impl_ifndefs=$(grep -rnE '^[[:space:]]*#[[:space:]]*ifndef[[:space:]]+[A-Za-z0-9_]+_IMPL([[:space:]]|$)' "$REPO_ROOT" \
	--exclude-dir=node_modules --exclude-dir=.git --exclude-dir=.pi --include='*.h' || true)

if [ -n "$impl_ifndefs" ]; then
	echo "FAIL: Found #ifndef *_IMPL guard in header(s):" >&2
	echo "$impl_ifndefs" >&2
	failed=1
fi

# Pass 1b: Check for #define *_IMPL in any *.c or *.h (excluding XY_IMPL macro definition)
impl_defines=$(grep -rnE '^[[:space:]]*#[[:space:]]*define[[:space:]]+[A-Za-z0-9_]+_IMPL([[:space:]]|$)' "$REPO_ROOT" \
	--exclude-dir=node_modules --exclude-dir=.git --exclude-dir=.pi --include='*.c' --include='*.h' || true)

if [ -n "$impl_defines" ]; then
	echo "FAIL: Found #define *_IMPL in source file(s):" >&2
	echo "$impl_defines" >&2
	failed=1
fi

# Pass 2: Check that no TU defining XY_IMPL(..., H, ...) includes a header declaring XY_DECL(..., H, ...)
while IFS= read -r c_file; do
	[ -f "$c_file" ] || continue
	hooks=$(grep -E 'XY_IMPL\([[:space:]]*[A-Za-z0-9_ *]+,[[:space:]]*[A-Za-z0-9_]+' "$c_file" 2>/dev/null | \
		sed -E 's/.*XY_IMPL\([[:space:]]*[^,]+,[[:space:]]*([A-Za-z0-9_]+).*/\1/' || true)
	[ -n "$hooks" ] || continue

	includes=$(grep -E '^[[:space:]]*#[[:space:]]*include[[:space:]]*["<][^">]+[">]' "$c_file" 2>/dev/null | \
		sed -E 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*["<]([^">]+)[">].*/\1/' || true)
	c_dir=$(dirname "$c_file")

	for hook in $hooks; do
		for inc in $includes; do
			hdr=""
			if [ -f "$c_dir/$inc" ]; then
				hdr="$c_dir/$inc"
			elif [ -f "$REPO_ROOT/include/$inc" ]; then
				hdr="$REPO_ROOT/include/$inc"
			elif [ -f "$REPO_ROOT/external/axil-auth/include/$inc" ]; then
				hdr="$REPO_ROOT/external/axil-auth/include/$inc"
			elif [ -f "$REPO_ROOT/external/libxylem/include/$inc" ]; then
				hdr="$REPO_ROOT/external/libxylem/include/$inc"
			else
				for d in "$REPO_ROOT"/external/*/include; do
					if [ -f "$d/$inc" ]; then
						hdr="$d/$inc"
						break
					fi
				done
			fi
			if [ -n "$hdr" ] && [ -f "$hdr" ]; then
				if grep -q -E "XY_DECL\([[:space:]]*[^,]+,[[:space:]]*${hook}[[:space:]]*," "$hdr" 2>/dev/null; then
					echo "FAIL: $c_file implements XY hook '$hook' but includes '$inc' which declares it" >&2
					failed=1
				fi
			fi
		done
	done
done <<EOF
$(find "$REPO_ROOT/mods" "$REPO_ROOT/external" -name '*.c' -not -path '*/.*' -not -path '*/node_modules/*')
EOF

if [ "$failed" -eq 0 ]; then
	echo "PASS: Zero *_IMPL guards found, two-header separation rule verified."
	exit 0
else
	exit 1
fi
