#!/bin/sh
# axil-nd must never load an nd-* module statically. Kernel libs arrive via the
# `axil -m` list (scripts/nd-kernel-mods.sh); content modules via in-game
# `loadmod`, persisted in region rows. Forbidden: xy_load of an nd lib in
# axil-nd, an nd-* link flag on libaxil-nd, an nd-* entry in mods.load.
cd "$(dirname "$0")/.." || exit 1
fail=0
hits=$(grep -rnE 'xy_load[[:space:]]*\([^)]*nd-' external/axil-nd/src external/axil-nd/include --include='*.c' --include='*.h' 2>/dev/null)
[ -n "$hits" ] && { echo "FAIL: axil-nd xy_loads an nd-* module:"; echo "$hits"; fail=1; }
hits=$(grep -nE '^(LDLIBS|LDFLAGS)[^ ]*libaxil-nd.*(-l|lib)nd-' external/axil-nd/Makefile external/axil-nd/*.mk 2>/dev/null)
[ -n "$hits" ] && { echo "FAIL: libaxil-nd links an nd-* lib:"; echo "$hits"; fail=1; }
hits=$(grep -nE '(^|/)(lib)?nd-' mods.load external/axil-nd/mods.load 2>/dev/null)
[ -n "$hits" ] && { echo "FAIL: mods.load lists an nd-* module:"; echo "$hits"; fail=1; }
so=external/axil-nd/lib/libaxil-nd.so
if [ -f "$so" ] && command -v readelf >/dev/null 2>&1; then
	hits=$(readelf -d "$so" | grep NEEDED | grep 'libnd-')
	[ -n "$hits" ] && { echo "FAIL: $so has nd-* DT_NEEDED:"; echo "$hits"; fail=1; }
fi
[ $fail -eq 0 ] && echo "OK: no static nd-* loads"
exit $fail
