#!/bin/sh
set -eu

# A DT_RUNPATH into the source tree makes the tree the runtime authority for
# that library while xy_load("lib...") still resolves the same soname from the
# system search path: the loader then maps two distinct files as two distinct
# objects, only one of which gets the xy context bound into it. Calls made
# through the other copy dispatch on a zero context and fault. Ship artifacts
# must therefore resolve every library from the system path alone -- and the
# site link inputs must not force a search path either (external/mk already
# adds ${prefix}/lib for the packages; a forced -L is how the second authority
# gets back in).

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
failed=0

if ! command -v readelf >/dev/null 2>&1; then
	printf 'check-no-tree-rpath: readelf not found, cannot verify runpaths\n' >&2
	exit 1
fi

for f in "$root"/mods/*/*.so "$root"/external/*/lib/*.so "$root"/external/axil/bin/axil; do
	[ -f "$f" ] || continue
	runpath=$(readelf -d "$f" 2>/dev/null |
		grep -E 'RPATH|RUNPATH' || true)
	[ -n "$runpath" ] || continue
	case "$runpath" in
	*"$root"*)
		printf '%s: source-tree runpath recorded:\n%s\n' "${f#"$root"/}" "$runpath" >&2
		failed=1
		;;
	esac
done

for f in "$root/build.mk" "$root/Makefile" "$root"/mods/*/Makefile; do
	[ -f "$f" ] || continue
	hit=$(grep -n -- '-L' "$f" | grep -v ':[[:space:]]*#' || true)
	[ -z "$hit" ] || {
		printf '%s: forced library search path (link must use the system path):\n%s\n' \
			"${f#"$root"/}" "$hit" >&2
		failed=1
	}
done

if [ "$failed" -eq 0 ]; then
	printf 'check-no-tree-rpath: ok\n'
fi
exit "$failed"
