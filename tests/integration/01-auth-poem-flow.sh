#!/bin/sh
set -e

HOST="localhost"
PORT="3001"
BASE="http://$HOST:$PORT"
LOG="/tmp/auth_poem_test_ndc.log"
COOKIE="/tmp/auth_poem_test_cookie"
RCODE=""

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

TMPFILE="/tmp/poem_test_$$"

cleanup() {
	pkill -f "ndc.*$PORT" 2>/dev/null || true
	rm -f "$LOG" "$COOKIE" auth.qmap "$TMPFILE"
	rm -rf /home/quirinpa/site/items/poem/items
}

start_server() {
	cleanup
	sleep 1
	rm -f auth.qmap
	mkdir -p /home/quirinpa/site/items/poem/items
	LD_LIBRARY_PATH=/home/quirinpa/ndc/lib:/home/quirinpa/qmap/lib /home/quirinpa/ndc/bin/ndc -C /home/quirinpa/site -p $PORT -d 2>"$LOG" &
	sleep 3
}

get_rcode() {
	RCODE=$(grep "Register" "$LOG" | tail -1 | sed 's/.*r=\([a-f0-9]*\).*/\1/')
}

echo "=== Auth + Poem Integration Tests ==="
start_server

# 1. Register a user
echo -n "1. Register user... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/register" \
	-d "username=testuser&password=pass1234&password2=pass1234&email=test@test.com")
[ "$code" = "303" ] && pass "registered" || fail "expected 303, got $code"
get_rcode

# 2. Confirm user
echo -n "2. Confirm user... "
code=$(curl -sw "%{http_code}" -o /dev/null "$BASE/confirm?u=testuser&r=$RCODE")
[ "$code" = "303" ] && pass "confirmed" || fail "expected 303, got $code"

# 3. Login
echo -n "3. Login... "
code=$(curl -sw "%{http_code}" -o /dev/null -c "$COOKIE" -X POST "$BASE/login" \
	-d "username=testuser&password=pass1234")
[ "$code" = "303" ] && pass "logged in" || fail "expected 303, got $code"

# 4. Verify logged in
echo -n "4. Verify session... "
out=$(curl -sb "$COOKIE" "$BASE/api/session")
[ "$out" = "testuser" ] && pass "session valid" || fail "expected testuser, got: $out"

# 5. Upload a poem (authenticated)
echo -n "5. Upload poem... "
echo "Test poem content" > "$TMPFILE"
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" -X POST "$BASE/poem/add" \
	-F "id=testpoem" -F "file=@$TMPFILE")
[ "$code" = "303" ] && pass "poem uploaded" || fail "expected 303, got $code"

# 6. Verify poem file exists
echo -n "6. Poem file created... "
[ -f /home/quirinpa/site/items/poem/items/testpoem/pt_PT.html ] && pass "file exists" || fail "file not found"

# 7. Logout
echo -n "7. Logout... "
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" "$BASE/logout")
[ "$code" = "303" ] && pass "logged out" || fail "expected 303, got $code"

# 8. Verify logged out
echo -n "8. Verify logged out... "
out=$(curl -sb "$COOKIE" "$BASE/api/session")
[ -z "$out" ] && pass "session cleared" || fail "expected empty, got: $out"

cleanup
echo ""
echo "All integration tests passed!"
