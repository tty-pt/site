#!/bin/sh
set -eu
# check-wasm-imports.sh — WASM import audit (W06 / D18).
#
# Every import of every htdocs/*.wasm must be `<namespace>.<name>` listed in
# the owning library's wasm-allowed-imports.lst (external/*/), or be WASI.
# Consequences:
#   - no `env.*` imports: the build links without --allow-undefined, so an
#     env import can only come from an undeclared host symbol, and the browser
#     host would have to stub it (that is how XXH32 and json-c silently
#     returned 0 in nd.wasm / chat.wasm);
#   - no native site symbols (corm_/source_/axil_/xy_/hyle_source/XY_);
#   - a namespace is only usable if the library that implements its host
#     bridge declares the symbol.

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"

if ! command -v wasm-objdump >/dev/null 2>&1; then
	echo "FAIL: wasm-objdump (wabt) is required for the WASM import audit" >&2
	exit 1
fi

allow=$(mktemp)
trap 'rm -f "$allow"' EXIT
for f in external/*/wasm-allowed-imports.lst; do
	[ -f "$f" ] && grep -Ev '^[[:space:]]*(#|$)' "$f"
done | sort -u > "$allow"

FAIL=0
targets="${*:-htdocs/*.wasm}"
for wasm in $targets; do
	[ -f "$wasm" ] || continue
	echo "checking $wasm"
	imports=$(wasm-objdump -j Import -x "$wasm" 2>/dev/null |
		sed -n 's/.*<- \([A-Za-z0-9_.-]*\)$/\1/p')
	for imp in $imports; do
		case "$imp" in
		wasi_snapshot_preview1.*) continue ;;
		esac
		if grep -qxF "$imp" "$allow"; then
			continue
		fi
		case "$imp" in
		env.*) why="undeclared host symbol (compile it in or declare it with ISO_IMPORT)" ;;
		*.corm_*|*.source_*|*.axil_*|*.xy_*|*.hyle_source*|*.XY_*) why="native site symbol" ;;
		*) why="not in any external/*/wasm-allowed-imports.lst" ;;
		esac
		echo "FAIL: $wasm imports $imp — $why"
		FAIL=1
	done
done

if [ "$FAIL" -ne 0 ]; then
	echo "W06 check FAILED: unsanctioned WASM imports"
	exit 1
fi
echo "W06 check PASS: all WASM imports sanctioned"
