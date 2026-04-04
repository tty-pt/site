#!/bin/sh
set -e

BASE="http://localhost:8080"
COOKIE="/tmp/auth_test_cookie_$$"
LOG="/tmp/site.log"
RCODE=""
USER="testuser_$$"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

get_rcode() {
	RCODE=$(grep "auth/confirm" "$LOG" | tail -1 | sed 's/.*r=\([a-f0-9]*\).*/\1/')
}

# 1. Empty session
echo -n "1. Empty session... "
out=$(curl -sb "$COOKIE" "$BASE/auth/api/session")
[ -z "$out" ] && pass "empty session" || fail "expected empty, got: $out"

# 2. Register valid user
echo -n "2. Register valid user... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/auth/register" \
	-d "username=$USER&password=pass1234&password2=pass1234&email=test@test.com")
[ "$code" = "303" ] && pass "register redirects" || fail "expected 303, got $code"
get_rcode

# 3. Login before confirm
echo -n "3. Login before confirm... "
out=$(curl -s -X POST "$BASE/auth/login" -d "username=$USER&password=pass1234")
echo "$out" | grep -q "not confirmed" && pass "not confirmed" || fail "expected 'not confirmed', got: $out"

# 4. Confirm
echo -n "4. Confirm with rcode... "
code=$(curl -sw "%{http_code}" -o /dev/null "$BASE/auth/confirm?u=$USER&r=$RCODE")
[ "$code" = "303" ] && pass "confirm redirects" || fail "expected 303, got $code"

# 5. Login after confirm
echo -n "5. Login after confirm... "
code=$(curl -sw "%{http_code}" -o /dev/null -c "$COOKIE" -X POST "$BASE/auth/login" \
	-d "username=$USER&password=pass1234")
[ "$code" = "303" ] && pass "login redirects" || fail "expected 303, got $code"

# 6. Session with cookie
echo -n "6. Session with cookie... "
out=$(curl -sb "$COOKIE" "$BASE/auth/api/session")
[ "$out" = "$USER" ] && pass "session returns user" || fail "expected '$USER', got: $out"

# 7. Logout
echo -n "7. Logout... "
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" "$BASE/auth/logout")
[ "$code" = "303" ] && pass "logout redirects" || fail "expected 303, got $code"

# 8. Session after logout
echo -n "8. Session after logout... "
out=$(curl -sb "$COOKIE" "$BASE/auth/api/session")
[ -z "$out" ] && pass "session empty" || fail "expected empty, got: $out"

# 9. Register duplicate
echo -n "9. Register duplicate... "
out=$(curl -s -X POST "$BASE/auth/register" -d "username=$USER&password=pass1234&password2=pass1234&email=test2@test.com")
echo "$out" | grep -qi "exists" && pass "duplicate rejected" || fail "expected 'exists', got: $out"

# 10. Login wrong password
echo -n "10. Login wrong password... "
out=$(curl -s -X POST "$BASE/auth/login" -d "username=$USER&password=wrongpass")
echo "$out" | grep -q "Invalid" && pass "wrong password rejected" || fail "expected 'Invalid', got: $out"

# 11. Login nonexistent user
echo -n "11. Login nonexistent user... "
out=$(curl -s -X POST "$BASE/auth/login" -d "username=nobody_$$&password=pass1234")
echo "$out" | grep -qi "not found" && pass "nonexistent rejected" || fail "expected 'not found', got: $out"
