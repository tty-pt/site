#!/bin/sh
# Migrate .owner files to filesystem uid ownership.
# Run once as root after deploying the uid-based ownership code.
SITE=/home/quirinpa/site
PASSWD=$SITE/etc/passwd

find "$SITE/items" -name ".owner" | while read f; do
	dir=$(dirname "$f")
	username=$(cat "$f")
	uid=$(awk -F: -v u="$username" '$1==u{print $3}' "$PASSWD")
	if [ -n "$uid" ]; then
		chown "$uid" "$dir"
		echo "chowned $dir -> uid $uid ($username)"
	else
		echo "WARN: no uid for '$username' in $dir"
	fi
	rm "$f"
done
