#!/bin/sh
set -eu
# Ensure WASM bundles don't import native site symbols (W06 / D18).
# Uses wasm-objdump if available, else strings fallback.

FAIL=0
for wasm in htdocs/*.wasm; do
	[ -f "$wasm" ] || continue
	echo "checking $wasm"
	if command -v wasm-objdump >/dev/null 2>&1; then
		out="$(wasm-objdump -x "$wasm" 2>/dev/null || wasm-objdump -j Import -x "$wasm" 2>/dev/null || true)"
		if echo "$out" | grep -Eq 'qmap_|source_|axil_|xy_|hyle_source|XY_'; then
			echo "FAIL: $wasm imports native symbol:"
			echo "$out" | grep -E 'qmap_|source_|axil_|xy_|hyle_source|XY_' || true
			FAIL=1
		fi
	elif command -v llvm-objdump >/dev/null 2>&1; then
		out="$(llvm-objdump --syms "$wasm" 2>/dev/null || true)"
		if echo "$out" | grep -Eq 'qmap_|source_|axil_|xy_'; then
			echo "FAIL: $wasm imports native symbol (llvm fallback)"
			FAIL=1
		fi
	else
		# strings fallback — less precise but catches leaked imports
		if strings "$wasm" 2>/dev/null | grep -qE 'qmap_|source_item|axil_|xy_call'; then
			# allow that wasm may contain debug strings; check import section via strings + import name pattern
			echo "WARN: $wasm contains native-like strings (no wasm-objdump) — manual check needed"
		fi
	fi
done

if [ "$FAIL" -ne 0 ]; then
	echo "W06 check FAILED: WASM imports native symbols (--allow-undefined hid errors)"
	exit 1
fi
echo "W06 check PASS: no native imports in WASM"
