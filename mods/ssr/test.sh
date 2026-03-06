#!/bin/sh
set -e

BASE="http://localhost:8080"
LOG="/tmp/site.log"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

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
