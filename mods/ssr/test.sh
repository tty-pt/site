#!/bin/sh
set -e

HOST="localhost"
PORT="3001"
BASE="http://$HOST:$PORT"
LOG="/tmp/ssr_test_ndc.log"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

cleanup() {
	pkill -f "ndc.*$PORT" 2>/dev/null || true
	pkill -f "deno" 2>/dev/null || true
	rm -f "$LOG"
}

start_server() {
	cleanup
	sleep 1
	LD_LIBRARY_PATH=/home/quirinpa/ndc/lib:/home/quirinpa/qmap/lib /home/quirinpa/ndc/bin/ndc -C /home/quirinpa/site -p $PORT -d 2>"$LOG" &
	sleep 3
}

echo "=== SSR Module Tests ==="
start_server

# 1. SSR module loads
echo -n "1. SSR module loads... "
grep -q "ssr" "$LOG" && pass "SSR loaded" || fail "SSR not loaded"

# 2. Unknown routes fall through to SSR (return HTML, not raw 404)
echo -n "2. Unknown routes return SSR HTML... "
out=$(curl -s "$BASE/nonexistent-page-12345")
echo "$out" | grep -q "<html" && pass "SSR fallback works" || fail "not SSR fallback: $out"

# 3. SSR handles missing Deno gracefully (returns error page)
echo -n "3. SSR handles missing Deno gracefully... "
echo "$out" | grep -qi "deno" && pass "Deno error shown" || fail "unexpected response"

# 4. SSR response includes body tag
echo -n "4. SSR includes body tag... "
echo "$out" | grep -q "<body>" && pass "body tag present" || fail "no body tag"

cleanup
echo ""
echo "All tests passed!"
