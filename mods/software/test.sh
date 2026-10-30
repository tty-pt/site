#!/bin/sh
set -e

BASE="http://localhost:8080"
REPO_ROOT="$(cd ../.. && pwd)"
SOFTWARE_DIR="$REPO_ROOT/var/software"
COOKIE="/tmp/software_test_cookie_$$"
USER="softtest$$"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

cleanup() {
	rm -f "$COOKIE" "/tmp/soft_test_$$"*
	rm -rf "$SOFTWARE_DIR/$USER"
}
trap cleanup EXIT INT TERM

TMPFILE="/tmp/soft_test_$$"

# Extract csrf_token value from cookie jar
csrf_for() {
	grep csrf_token "$1" 2>/dev/null | awk '{print $NF}' | head -1
}

echo "=== Testing mods/software ==="

# 1. Listing GET /software/
echo -n "1. GET /software/... "
res=$(curl -s "$BASE/software/")
echo "$res" | grep -qi "Software" && pass "listing loaded" || fail "listing missing 'Software'"

# 2. Check seeded items appear in listing
echo -n "2. Verify seeded items in listing... "
echo "$res" | grep -q "libcorm" && echo "$res" | grep -q "axil-auren" && echo "$res" | grep -q "pi-mm" \
	&& pass "seeded items present" || fail "seeded items not found in listing"

# 3. Detail page GET /software/corm
echo -n "3. GET /software/corm detail page... "
corm_html=$(curl -s "$BASE/software/corm")
echo "$corm_html" | grep -q "libcorm" || fail "detail page missing libcorm"
echo "$corm_html" | grep -q "https://github.com/tty-pt/corm" || fail "missing GitHub link"
echo "$corm_html" | grep -q "v2.0.0" || fail "missing version badge"
echo "$corm_html" | grep -q "BSD-2-Clause" || fail "missing license badge"
echo "$corm_html" | grep -q "<h1" || fail "missing markdown rendered h1"
echo "$corm_html" | grep -q "<h2" || fail "missing markdown rendered h2"
echo "$corm_html" | grep -q "<code" || fail "missing markdown rendered code"
pass "corm detail page rendered with parsed markdown and github link"

# 4. Detail page GET /software/pi-mm
echo -n "4. GET /software/pi-mm detail page... "
pimm_html=$(curl -s "$BASE/software/pi-mm")
echo "$pimm_html" | grep -q "pi-mm" || fail "detail page missing pi-mm"
echo "$pimm_html" | grep -q "https://github.com/tty-pt/pi-mm" || fail "missing GitHub link"
echo "$pimm_html" | grep -q "v0.4.1" || fail "missing version badge"
echo "$pimm_html" | grep -q "Memory Mipmaps" || fail "missing summary"
pass "pi-mm detail page rendered with parsed markdown and github link"

# 5. Register and login as test user
echo -n "5. Register test user... "
code=$(curl -sw "%{http_code}" -o /dev/null -c "$COOKIE" -X POST "$BASE/auth/register" \
	-d "username=$USER&password=pass1234&password2=pass1234&email=test@test.com")
[ "$code" = "303" ] && pass "registered" || fail "expected 303, got $code"

# Seed cookie jar with a csrf_token
curl -s -b "$COOKIE" -c "$COOKIE" "$BASE/software/add" > /dev/null

# 6. Add a new software entry via POST /software/add
echo -n "6. POST /software/add... "
echo "# Custom Soft\n\nThis is a **custom** test project with [docs](https://github.com/tty-pt/custom)." > "$TMPFILE"
csrf=$(csrf_for "$COOKIE")
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" \
	-X POST "$BASE/software/add" \
	-F "title=$USER" \
	-F "repo_url=https://github.com/tty-pt/$USER" \
	-F "summary=A dynamic test module" \
	-F "version=1.0.0" \
	-F "license=MIT" \
	-F "category=Test" \
	-F "body_content=@$TMPFILE" \
	-F "csrf_token=$csrf")
[ "$code" = "303" ] && pass "add redirects" || fail "expected 303, got $code"

# 7. Verify newly created software detail page
echo -n "7. Verify detail of added software... "
new_html=$(curl -s "$BASE/software/$USER")
echo "$new_html" | grep -q "$USER" || fail "missing title in detail"
echo "$new_html" | grep -q "https://github.com/tty-pt/$USER" || fail "missing repo link"
echo "$new_html" | grep -q "<strong>custom</strong>" || fail "missing markdown bold"
echo "$new_html" | grep -q "https://github.com/tty-pt/custom" || fail "missing markdown parsed link"
pass "added software detail page verified"

# 8. Delete newly added software
echo -n "8. Delete added software... "
csrf=$(csrf_for "$COOKIE")
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" \
	-X POST "$BASE/software/$USER/delete" \
	-F "csrf_token=$csrf")
[ "$code" = "303" ] && pass "delete redirects" || fail "expected 303, got $code"

echo "=== All mods/software tests PASS ==="
