#!/bin/sh
# seed-jail.sh -- populate the axil -C chroot jail with what the engine and
# the site resolve AFTER the chroot (the manifest, and why, is in
# scripts/jail-manifest.sh).
#
#   sh scripts/seed-jail.sh [JAIL]     # JAIL defaults to $JAIL or /var/www
#
# The jail is normally the site root itself, so the htdocs trees are already
# in place there and are skipped; what this script adds is the installed game
# libraries under usr/local/lib and the runtime libraries under usr/lib.
# Never run it at a jail other than the site root -- check-jail.sh refuses
# one, because corm would load an empty world from it and save the world
# back to it.
#
# Run it after `make` (and `make install-libs`, which is what builds the
# nd-* modules) and before the server restarts: sh scripts/check-jail.sh.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"
. scripts/jail-manifest.sh

JAIL=${1:-${JAIL:-/var/www}}

if ! command -v readelf >/dev/null 2>&1; then
	printf 'seed-jail: readelf not found, cannot derive DT_NEEDED\n' >&2
	exit 1
fi

# The startup closure: which DT_NEEDEDs are already mapped when the process
# comes up, and therefore need no copy inside the jail.
if [ -z "${JAIL_AXIL_BIN:-}" ]; then
	if [ -x external/axil/bin/axil ]; then
		JAIL_AXIL_BIN=external/axil/bin/axil
	else
		JAIL_AXIL_BIN=$(command -v axil 2>/dev/null || true)
	fi
fi

if [ ! -d "$JAIL" ]; then
	printf 'seed-jail: %s is not a directory\n' "$JAIL" >&2
	exit 1
fi

manifest=$(jail_manifest) || {
	printf 'seed-jail: manifest failed, nothing copied\n' >&2
	exit 1
}

# Byte-identical counts as already in place: re-copying a correct jail on
# every run would churn mtimes for no reason. On the supported topology
# (jail == site root) the tree entries are the jail's own files and land here
# too, so the seed never copies a file onto itself.
copied=0
kept=0
while read -r src dst; do
	[ -n "$src" ] || continue
	if [ ! -e "$src" ]; then
		printf 'seed-jail: missing source %s -- build it first (make, then make install-libs)\n' \
			"$src" >&2
		exit 1
	fi
	src_path=$(realpath "$src")
	dst_path=$JAIL/$dst
	if [ -f "$dst_path" ] && cmp -s "$src_path" "$dst_path"; then
		kept=$((kept + 1))
		continue
	fi
	mkdir -p "$(dirname "$dst_path")"
	# rm first: the destination may be a symlink left by an older seed, and
	# cp would write through it. The source is already a resolved path, so
	# the result is always a regular file -- which is what module_load_path()'s
	# realpath() and glibc's search both expect to find.
	rm -f "$dst_path"
	cp -p "$src_path" "$dst_path"
	copied=$((copied + 1))
done <<EOF
$manifest
EOF

printf 'seed-jail: %s file(s) copied, %s already in place in %s\n' \
	"$copied" "$kept" "$JAIL"
printf 'seed-jail: now verify it: sh scripts/check-jail.sh %s\n' "$JAIL"
