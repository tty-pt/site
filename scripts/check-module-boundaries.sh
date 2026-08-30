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
	mods/common/ux/site_ui.c|mods/index/ux/list.c)
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

# -- var/ literals outside storage/registration ---------------------------------
# Allowed: storage implementation + source adapter + registration declarations
# (COMPLY §3.5 / §9.6). Feature modules declare their var/ only as
# source_setup("…","var/<module>") — allow site modules.
while IFS=: read -r file line text; do
	[ -z "$file" ] && continue
	case "$file" in
		mods/common/common_storage.c|mods/common/common_storage.h|mods/source/*|mods/*/*) continue ;;
	esac
	printf '%s:%s: prohibited var/ literal outside storage/registration: %s\n' "$file" "$line" "$text" >&2
	failed=1
done <<EOF
$(CDPATH= cd -- "$root" && rg -n '"[^"]*var/' mods --glob '*.c' --glob '*.h' 2>/dev/null || true)
EOF

# -- getpwuid must not appear in site modules (M06) -----------------------------
while IFS=: read -r file line text; do
	[ -z "$file" ] && continue
	printf '%s:%s: prohibited getpwuid in site module (use auth owner API): %s\n' "$file" "$line" "$text" >&2
	failed=1
done <<EOF
$(CDPATH= cd -- "$root" && rg -n 'getpwuid' mods --glob '*.c' --glob '*.h' 2>/dev/null || true)
EOF

# -- xy_load allowlist: site modules may declare immediate true deps ------------
# libxylem is the only cross-.so mechanism; ensure no hard-coded dlopen etc.
while IFS=: read -r file line text; do
	[ -z "$file" ] && continue
	case "$file" in
		mods/*/*) continue ;;
	esac
	printf '%s:%s: unexpected xy_load outside site modules: %s\n' "$file" "$line" "$text" >&2
	failed=1
done <<EOF
$(CDPATH= cd -- "$root" && rg -n 'xy_load' mods --glob '*.c' --glob '*.h' 2>/dev/null || true)
EOF

# -- module-name switch in index (M07) ---------------------------------------
while IFS=: read -r file line text; do
	[ -z "$file" ] && continue
	printf '%s:%s: prohibited module-name switch in index (M07): %s\n' "$file" "$line" "$text" >&2
	failed=1
done <<EOF
$(CDPATH= cd -- "$root" && rg -n 'idx_select_fields_for|idx_display_name|strcmp\([^)]*"(song|poem|gig|grp)"' mods/index --glob '*.c' --glob '*.h' 2>/dev/null || true)
EOF

# -- direct hyle row writes bypassing source (M05) ----------------------------
# All item and ordered source writes must use source module operations.
while IFS=: read -r file line text; do
	[ -z "$file" ] && continue
	case "$file" in
		mods/source/*|external/hyle/*) continue ;;
	esac
	printf '%s:%s: prohibited direct hyle_source_put/del outside source (use source ops): %s\n' "$file" "$line" "$text" >&2
	failed=1
done <<EOF
$(CDPATH= cd -- "$root" && rg -n 'hyle_source_(put|del|register)' mods --glob '*.c' --glob '*.h' 2>/dev/null | grep -v 'mods/source/source\.c:.*hyle_source_register' || true)
EOF

# -- site-specific surface minimal (blocking): no hardcoded module names in common/index outside registration -----
# Common is reusable within site; site-specific icons/CSP/menu must be per-module registration, not switch in common/index.
# Grandfathered: mods/common/ux/site_paths.c:68 icon table is the only allowed site enumeration — do not add more.
while IFS=: read -r file line text; do
	[ -z "$file" ] && continue
	case "$file" in
		mods/common/ux/site_paths.c) continue ;;
	esac
	printf '%s:%s: site-specific surface in common/index (use per-module source_list_view_t / site registration, not hardcoded "poem"/"song"/"gig"/"grp"): %s\n' "$file" "$line" "$text" >&2
	failed=1
done <<EOF
$(CDPATH= cd -- "$root" && rg -n '"(poem|song|gig|grp)"' mods/common mods/index --glob '*.c' --glob '*.h' 2>/dev/null || true)
EOF

# -- W06 wasm-native leakage (D18) ------------------------------------------
if ls "$root"/htdocs/*.wasm >/dev/null 2>&1; then
	if ! sh "$root/scripts/check-wasm-imports.sh" >/dev/null 2>&1; then
		printf 'W06: wasm imports native symbols (check scripts/check-wasm-imports.sh)\n' >&2
		failed=1
	fi
fi

exit "$failed"
