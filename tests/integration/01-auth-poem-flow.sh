#!/bin/sh
set -e

HOST="localhost"
PORT="${AXIL_PORT:-8080}"
BASE="http://$HOST:$PORT"
COOKIE="/tmp/auth_poem_test_cookie"
USER="poemuser_$$"
POEM_ID="testpoem_$$"

# Environment setup
SITE_DIR="${SITE_DIR:-$(pwd)}"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

TMPFILE="/tmp/poem_test_$$"

cleanup() {
	rm -f "$COOKIE" "$TMPFILE"
	rm -rf "$SITE_DIR/var/poem/$POEM_ID"
}

wait_for_server() {
	local tries=0
	while ! curl -s --max-time 1 "$BASE/" > /dev/null 2>&1; do
		tries=$((tries + 1))
		[ $tries -ge 30 ] && fail "Server not ready after 30s"
		sleep 1
	done
}

echo "=== Auth + Poem Integration Tests ==="
wait_for_server

# 1. Register a user
echo -n "1. Register user... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/auth/register" \
	-d "username=$USER&password=pass1234&password2=pass1234&email=$USER@test.com")
[ "$code" = "303" ] && pass "registered" || fail "expected 303, got $code"

# 2. Login
echo -n "2. Login... "
code=$(curl -sw "%{http_code}" -o /dev/null -c "$COOKIE" -b "$COOKIE" -X POST "$BASE/auth/login" \
	-d "username=$USER&password=pass1234")
[ "$code" = "303" ] && pass "logged in" || fail "expected 303, got $code"

# 3. Verify logged in
echo -n "3. Verify session... "
out=$(curl -sb "$COOKIE" "$BASE/auth/api/session")
[ "$out" = "$USER" ] && pass "session valid" || fail "expected $USER, got: $out"

# 4. Upload a poem (authenticated)
echo -n "4. Upload poem... "
echo "Test poem content" > "$TMPFILE"
csrf=$(curl -sb "$COOKIE" -c "$COOKIE" "$BASE/api/csrf")
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" -c "$COOKIE" -X POST "$BASE/poem/add" \
	-F "title=$POEM_ID" -F "body_content=@$TMPFILE" -F "csrf_token=$csrf")
[ "$code" = "303" ] && pass "poem uploaded" || fail "expected 303, got $code"

# 5. Verify poem file exists
echo -n "5. Poem file created... "
[ -f "$SITE_DIR/var/poem/$POEM_ID/pt_PT.html" ] && pass "file exists" || fail "file not found"

# 6. Logout
echo -n "6. Logout... "
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" -c "$COOKIE" "$BASE/auth/logout")
[ "$code" = "303" ] && pass "logged out" || fail "expected 303, got $code"

# 7. Verify logged out
echo -n "7. Verify logged out... "
out=$(curl -sb "$COOKIE" "$BASE/auth/api/session")
[ -z "$out" ] && pass "session cleared" || fail "expected empty, got: $out"

cleanup
echo ""
echo "All auth + poem tests passed!"
exit 0
