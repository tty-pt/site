#!/bin/sh
set -e

HOST="localhost"
PORT="3001"
BASE="http://$HOST:$PORT"
COOKIE="/tmp/auth_test_cookie"
LOG="/tmp/auth_test_ndc.log"
RCODE=""

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

cleanup() {
	pkill -f "ndc.*$PORT" 2>/dev/null || true
	rm -f "$COOKIE" "$LOG" auth.qmap
}

start_server() {
	cleanup
	rm -f auth.qmap
	/home/quirinpa/ndc/bin/ndc -C /home/quirinpa/site -p $PORT -d 2>"$LOG" &
	sleep 2
}

get_rcode() {
	RCODE=$(grep "Register" "$LOG" | tail -1 | sed 's/.*r=\([a-f0-9]*\).*/\1/')
}

echo "=== Auth Module Tests ==="
start_server

# 1. Empty session
echo -n "1. Empty session... "
out=$(curl -sb "$COOKIE" "$BASE/api/session")
[ -z "$out" ] && pass "empty session" || fail "expected empty, got: $out"

# 2. Register valid user
echo -n "2. Register valid user... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/register" \
	-d "username=testuser&password=pass1234&password2=pass1234&email=test@test.com")
[ "$code" = "303" ] && pass "register redirects" || fail "expected 303, got $code"
get_rcode

# 3. Login before confirm
echo -n "3. Login before confirm... "
out=$(curl -s -X POST "$BASE/login" -d "username=testuser&password=pass1234")
echo "$out" | grep -q "Not confirmed" && pass "not confirmed" || fail "expected 'Not confirmed', got: $out"

# 4. Confirm
echo -n "4. Confirm with rcode... "
code=$(curl -sw "%{http_code}" -o /dev/null "$BASE/confirm?u=testuser&r=$RCODE")
[ "$code" = "303" ] && pass "confirm redirects" || fail "expected 303, got $code"

# 5. Login after confirm
echo -n "5. Login after confirm... "
code=$(curl -sw "%{http_code}" -o /dev/null -c "$COOKIE" -X POST "$BASE/login" \
	-d "username=testuser&password=pass1234")
[ "$code" = "303" ] && pass "login redirects" || fail "expected 303, got $code"

# 6. Session with cookie
echo -n "6. Session with cookie... "
out=$(curl -sb "$COOKIE" "$BASE/api/session")
[ "$out" = "testuser" ] && pass "session returns user" || fail "expected 'testuser', got: $out"

# 7. Logout
echo -n "7. Logout... "
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" "$BASE/logout")
[ "$code" = "303" ] && pass "logout redirects" || fail "expected 303, got $code"

# 8. Session after logout
echo -n "8. Session after logout... "
out=$(curl -sb "$COOKIE" "$BASE/api/session")
[ -z "$out" ] && pass "session empty" || fail "expected empty, got: $out"

# 9. Register duplicate
echo -n "9. Register duplicate... "
out=$(curl -s -X POST "$BASE/register" -d "username=testuser&password=pass1234&password2=pass1234&email=test2@test.com")
echo "$out" | grep -q "Exists" && pass "duplicate rejected" || fail "expected 'Exists', got: $out"

# 10. Login wrong password
echo -n "10. Login wrong password... "
out=$(curl -s -X POST "$BASE/login" -d "username=testuser&password=wrongpass")
echo "$out" | grep -q "Invalid" && pass "wrong password rejected" || fail "expected 'Invalid', got: $out"

# 11. Login nonexistent user
echo -n "11. Login nonexistent user... "
out=$(curl -s -X POST "$BASE/login" -d "username=nobody&password=pass1234")
echo "$out" | grep -q "No user" && pass "nonexistent rejected" || fail "expected 'No user', got: $out"

cleanup
echo ""
echo "All tests passed!"
