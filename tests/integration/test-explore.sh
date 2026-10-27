#!/bin/bash
# 10-nd-seven-stage.sh -- real-browser check of the deterministic Zelda-like 2D stage
# north-west of the spawn: sprite art, lamb, gates, deterministic rooms, and a
# mini-map canvas that keeps its own frames. Skips when Playwright is missing.
set -u

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
AXIL_BIN="$REPO_ROOT/external/axil/bin/axil"
[ -x "$AXIL_BIN" ] || AXIL_BIN=axil
if ! command -v node >/dev/null || [ ! -d "$REPO_ROOT/node_modules/playwright" ]; then
	echo "SKIP: 10-nd-seven-stage (node/playwright not installed)"; exit 0
fi
TMP="$(mktemp -d /tmp/nd-sevenstage-XXXXXX)"
PID=""
cleanup() {
	[ -n "$PID" ] && { kill -TERM "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; }
	rm -rf "$TMP"
}
trap cleanup EXIT INT TERM
show_log() { echo "=== AXIL LOG ==="; cat "$TMP/axil.log" | tail -50; }
trap 'show_log; cleanup' EXIT INT TERM

. "$REPO_ROOT/scripts/nd-kernel-mods.sh"
export PATH="$REPO_ROOT/external/axil/bin:$PATH"
export LD_LIBRARY_PATH="$(nd_local_ld_path "$REPO_ROOT")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export AXIL_ND_HTDOCS="$REPO_ROOT/htdocs"
export AUTH_SKIP_CONFIRM=1

db="$TMP/u.db"; log="$TMP/axil.log"; port=$((29800 + RANDOM % 90))
bash "$REPO_ROOT/scripts/seed-nd-universe.sh" "$db" 1 > "$TMP/seed.log" 2>&1 \
	|| { echo "seeding failed"; tail -5 "$TMP/seed.log"; exit 2; }
(cd "$REPO_ROOT" && AXIL_ND_DB="$db" exec "$AXIL_BIN" -A -C "$REPO_ROOT" -p "$port" -d \
	-m "mods/core/core:$ND_KERNEL_MODS" > "$log" 2>&1) &
PID=$!
tries=100
until grep -q "st_init: st_restore: region id=0x0000000000000000" "$log" 2>/dev/null; do
	tries=$((tries - 1)); [ $tries -gt 0 ] || { echo "daemon did not start"; tail "$log"; exit 2; }
	sleep 0.1
done

(cd "$REPO_ROOT" && node tests/integration/test-explore.mjs "$port")
rc=$?
[ $rc -eq 0 ] && echo "PASS: 10-nd-seven-stage"
exit $rc
