#!/bin/sh
# external/axil-nd (generic MUCK engine) must not name tenant modules/concepts.
cd "$(dirname "$0")/.." || exit 1
hits=$(grep -rniE 'nd-(attr|biome|class|core|drink|equip|fight|level|mob|mortal|other|plant|race|seat|shop|spell|stone|vanilla|wts)|spell|equip|grimoire|paper.?doll|stat_sheet|citadel' \
	external/axil-nd/src external/axil-nd/include --include='*.c' --include='*.h' 2>/dev/null)
if [ -n "$hits" ]; then
	echo "FAIL: axil-nd names tenant-module concepts:"; echo "$hits"; exit 1
fi
# mods/nd host shell must not include module UI headers directly.
if grep -nE '#include.*(attr|equip|spell|fight|mortal)-ui\.h' mods/nd/ux/nd_ui.c mods/nd/nd.c; then
	echo "FAIL: nd host includes module UI header"; exit 1
fi
# Sibling domain modules under external/nd-* must not directly link each other via -lnd-*.
sibling_hits=$(grep -nE -- '-lnd-(obj|world|view|region)' external/nd-*/Makefile 2>/dev/null || true)
if [ -n "$sibling_hits" ]; then
	echo "FAIL: prohibited domain sibling linkage in external/nd-* Makefiles:"; echo "$sibling_hits"; exit 1
fi
echo "OK: nd encapsulation boundary"
