#!/bin/sh
# Create canonical owner files for legacy UID-owned item directories.
# Defaults to a dry run. Run with --apply while the server is stopped, then
# restart it so source startup scanning refreshes owner fields in hyle.
set -eu

SITE=${SITE:-/home/quirinpa/site}
PASSWD=${PASSWD:-$SITE/etc/passwd}
MODULES=${MODULES:-"poem song gig grp song.types"}
APPLY=0

if [ "${1:-}" = "--apply" ]; then
	APPLY=1
elif [ "$#" -ne 0 ]; then
	echo "usage: $0 [--apply]" >&2
	exit 2
fi

if [ ! -r "$PASSWD" ]; then
	echo "error: site passwd database is not readable: $PASSWD" >&2
	exit 1
fi

for module in $MODULES; do
	root=$SITE/var/$module
	[ -d "$root" ] || continue
	find "$root" -mindepth 1 -maxdepth 1 -type d ! -name '.*' -print
done |
while IFS= read -r dir; do
	owner_file=$dir/owner
	[ -e "$owner_file" ] && continue

	uid=$(stat -c %u "$dir" 2>/dev/null || stat -f %u "$dir")
	username=$(awk -F: -v uid="$uid" '
		$3 == uid { name = $1; count++ }
		END { if (count == 1) print name }
	' "$PASSWD")

	if [ -z "$username" ]; then
		echo "UNRESOLVED $dir (uid $uid)" >&2
		continue
	fi

	if [ "$APPLY" -eq 0 ]; then
		echo "WOULD WRITE $dir/owner = $username"
		continue
	fi

	tmp=$dir/.owner-migrate.$$
	if ! printf '%s' "$username" >"$tmp"; then
		rm -f "$tmp"
		echo "FAILED $dir" >&2
		continue
	fi
	chmod 0644 "$tmp"
	mv "$tmp" "$owner_file"
	echo "WROTE $owner_file = $username"
done
