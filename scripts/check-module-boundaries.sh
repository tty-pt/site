#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
failed=0

check_include(){
	file=$1
	line=$2
	text=$3
	include=$(printf '%s\n' "$text" | sed -n 's/^[[:space:]]*#include[[:space:]]*"\([^"]*\.c\)".*/\1/p')
	[ -n "$include" ] || return 0
	target=$(realpath "$(dirname "$root/$file")/$include") || {
		printf '%s:%s: unresolved source include: %s\n' "$file" "$line" "$include" >&2
		failed=1
		return 0
	}
	source_module=$(printf '%s\n' "$file" | cut -d/ -f2)
	target_rel=${target#"$root"/}
	target_module=$(printf '%s\n' "$target_rel" | cut -d/ -f2)

	if [ "$source_module" = "$target_module" ]; then
		return 0
	fi
	case "$target_rel" in
	mods/common/ux/site_ui.c|mods/index/ux/list.c|mods/song/ux/music.c)
		return 0
		;;
	esac

	printf '%s:%s: prohibited cross-module source include: %s\n' \
		"$file" "$line" "$target_rel" >&2
	failed=1
}

while IFS=: read -r file line text; do
	check_include "$file" "$line" "$text"
done <<EOF
$(CDPATH= cd -- "$root" && find mods -type f -name '*.c' -exec grep -n -H '^[[:space:]]*#include[[:space:]]*"[^"]*\.c"' {} +)
EOF

exit "$failed"
