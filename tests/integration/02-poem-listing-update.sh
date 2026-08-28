#!/bin/sh
set -e

HOST="localhost"
PORT="${AXIL_PORT:-8080}"
BASE="http://$HOST:$PORT"
COOKIE="/tmp/poem_listing_test_cookie"
POEM_USER="poemlistuser_$$"
POEM1="listpoem1_$$"
POEM2="listpoem2_$$"

# Environment setup
SITE_DIR="${SITE_DIR:-$(pwd)}"
POEM_DIR="$SITE_DIR/var/poem"
TMPFILE1="/tmp/poem_listing_test_1_$$"
TMPFILE2="/tmp/poem_listing_test_2_$$"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

cleanup() {
	rm -rf "$POEM_DIR/$POEM1" "$POEM_DIR/$POEM2"
	rm -f "$COOKIE" "$TMPFILE1" "$TMPFILE2"
}

wait_for_server() {
	local tries=0
	while ! curl -s --max-time 1 "$BASE/" > /dev/null 2>&1; do
		tries=$((tries + 1))
		[ $tries -ge 30 ] && fail "Server not ready after 30s"
		sleep 1
	done
}

echo "=== Poem Listing Update Integration Tests ==="
wait_for_server

# Register & login
curl -sw "" -o /dev/null -c "$COOKIE" -X POST "$BASE/auth/register" \
	-d "username=$POEM_USER&password=pass1234&password2=pass1234&email=$POEM_USER@test.com" 2>/dev/null || true
curl -sw "%{http_code}" -o /dev/null -c "$COOKIE" -b "$COOKIE" -X POST "$BASE/auth/login" \
	-d "username=$POEM_USER&password=pass1234" >/dev/null 2>&1

csrf_token() {
	curl -sb "$COOKIE" -c "$COOKIE" "$BASE/api/csrf"
}

# 1. Add first poem
echo -n "1. Add first poem... "
echo "This is the first test poem.
It has multiple lines." > "$TMPFILE1"
result=$(curl -sw "\n%{http_code}" -b "$COOKIE" -c "$COOKIE" -X POST "$BASE/poem/add" \
	-F "title=$POEM1" -F "body_content=@$TMPFILE1" -F "csrf_token=$(csrf_token)")
code=$(echo "$result" | tail -1)
body=$(echo "$result" | head -n -1)
if [ "$code" = "303" ]; then
	pass "first poem uploaded (303 redirect)"
else
	fail "expected 303, got $code. Response: $body"
fi

# 2. Verify first poem appears in listing
echo -n "2. First poem in listing... "
sleep 0.5
out=$(curl -sb "$COOKIE" "$BASE/poem/?per_page=100")
if echo "$out" | grep -q "$POEM1"; then
	pass "$POEM1 appears in listing"
else
	fail "$POEM1 not found in listing"
fi

# 3. Add second poem
echo -n "3. Add second poem... "
echo "This is the second test poem.
Also with multiple lines." > "$TMPFILE2"
result=$(curl -sw "\n%{http_code}" -b "$COOKIE" -c "$COOKIE" -X POST "$BASE/poem/add" \
	-F "title=$POEM2" -F "body_content=@$TMPFILE2" -F "csrf_token=$(csrf_token)")
code=$(echo "$result" | tail -1)
body=$(echo "$result" | head -n -1)
if [ "$code" = "303" ]; then
	pass "second poem uploaded (303 redirect)"
else
	fail "expected 303, got $code. Response: $body"
fi

# 4. Verify both poems appear in listing
echo -n "4. Both poems in listing... "
sleep 0.5
out=$(curl -sb "$COOKIE" "$BASE/poem/?per_page=100")
if echo "$out" | grep -q "$POEM1" && echo "$out" | grep -q "$POEM2"; then
	pass "both $POEM1 and $POEM2 in listing"
else
	fail "missing poems in listing"
fi

cleanup
echo ""
echo "All poem listing integration tests passed!"
exit 0
