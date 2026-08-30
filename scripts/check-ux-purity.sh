#!/bin/sh
set -eu

# Check UX purity: UX code (dual-compiled) must not contain XY hooks or
# preprocessor conditionals (except allowed patterns).
#
# Target: mods/*/ux/*.c + mods/common/ux/*.c
# Exit 0 always (warn only for now).

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

# Find all UX .c files
ux_files=$(find "$root/mods" -type f -path "*/ux/*.c" | sort)

# ---- Check 1: XY hooks in UX (hard violation of isolation) ----
xy_patterns='XY_DECL|XY_IMPL|xy_load|xy_install|XY_MODULE_API|XY_CALL'
echo "=== UX XY hooks check ==="
if grep -nE "$xy_patterns" $ux_files 2>/dev/null; then
    grep -nE "$xy_patterns" $ux_files 2>/dev/null | \
        while IFS=: read -r file line text; do
            printf 'WARN: %s:%s: XY hook in UX (not allowed): %s\n' "$file" "$line" "$text" >&2
        done
else
    echo "OK: no XY hooks in UX"
fi

# ---- Check 2: Preprocessor conditionals in UX (warn) ----
# Allowed patterns:
#   1. Include guards: #ifndef FOO_H / #ifndef FOO_C (line 1)
#   2. site_ui.c:#ifndef __wasm__ (native aggregator, line 9)
#   3. gig/ux/detail.c:#ifdef __wasm__ (WASM host imports, line 25)
#   4. site_page.c:#if __has_include("version.gen.h") (version fallback, line 109)
echo ""
echo "=== UX preprocessor check ==="

# Use a function to check if a file:line is in the allowed list
is_allowed() {
    case "$1" in
        # Include guards at line 1
        "$root/mods/common/ux/site_ui.c:1") return 0 ;;
        "$root/mods/common/ux/site_forms.c:1") return 0 ;;
        "$root/mods/common/ux/site_layout.c:1") return 0 ;;
        "$root/mods/common/ux/site_media.c:1") return 0 ;;
        "$root/mods/common/ux/site_page.c:1") return 0 ;;
        "$root/mods/common/ux/site_paths.c:1") return 0 ;;
        "$root/mods/common/ux/site_chrome.c:1") return 0 ;;
        "$root/mods/index/ux/list.c:1") return 0 ;;
        "$root/mods/index/ux/list_filters.c:1") return 0 ;;
        "$root/mods/index/ux/list_json.c:1") return 0 ;;
        "$root/mods/index/ux/list_layout.c:1") return 0 ;;
        "$root/mods/index/ux/list_query.c:1") return 0 ;;
        "$root/mods/index/ux/list_render.c:1") return 0 ;;
        # Specific allowed preprocessor uses
        "$root/mods/common/ux/site_ui.c:"*) return 0 ;;
        "$root/mods/gig/ux/detail.c:"*) return 0 ;;
        "$root/mods/common/ux/site_page.c:"*) return 0 ;;
    esac
    return 1
}

# Find all #if/#ifdef/#ifndef lines in UX files, filter out allowed
found_violation=0
grep -n '^[[:space:]]*#if\(def\|ndef\)\?' $ux_files 2>/dev/null | \
while IFS=: read -r file line text; do
    if ! is_allowed "$file:$line"; then
        printf 'WARN: %s:%s: preprocessor conditional in UX (branch on runtime state instead): %s\n' "$file" "$line" "$text" >&2
        found_violation=1
    fi
done

# Check if any unexpected found (run again to catch violations)
violations=$(grep -n '^[[:space:]]*#if\(def\|ndef\)\?' $ux_files 2>/dev/null | \
    while IFS=: read -r file line text; do
        if ! is_allowed "$file:$line"; then
            printf '%s:%s:%s\n' "$file" "$line" "$text"
        fi
    done)

if [ -n "$violations" ]; then
    echo "Found unexpected preprocessor conditionals (see warnings above)"
else
    echo "OK: no unexpected preprocessor conditionals in UX"
fi

# Always exit 0 (warn only for now)
exit 0