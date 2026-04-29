#!/bin/sh
set -e

BASE="http://localhost:8080"
POEM_DIR="/home/quirinpa/site/items/poem/items"
COOKIE="/tmp/poem_test_cookie_$$"
USER="poemtest$$"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

TMPFILE="/tmp/poem_test_$$"
TMPFILE2="/tmp/poem_test2_$$"

# Register and login as test user
echo -n "0. Register test user... "
code=$(curl -sw "%{http_code}" -o /dev/null -c "$COOKIE" -X POST "$BASE/auth/register" \
	-d "username=$USER&password=pass1234&password2=pass1234&email=test@test.com")
[ "$code" = "303" ] && pass "registered" || fail "expected 303, got $code"

# 1. Add a poem via /poem/add first so the item exists and is owned by our user
echo -n "1. Add poem via /poem/add... "
echo "<p>Initial content.</p>" > "$TMPFILE"
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" -c "$COOKIE" \
	-X POST "$BASE/poem/add" \
	-F "title=$USER" -F "file=@$TMPFILE")
[ "$code" = "303" ] && pass "add redirects" || fail "expected 303, got $code"

# 1b. POST edit with wrong content-type on owned item (expect 415)
echo -n "1b. POST wrong content-type... "
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" -X POST "$BASE/poem/$USER/edit" \
	-d "title=test")
[ "$code" = "415" ] && pass "415 for unsupported media type" || fail "expected 415, got $code"

# 2. POST with only title (no file)
echo -n "2. POST title only... "
sleep 0.1
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" -X POST "$BASE/poem/$USER/edit" \
	-F "title=My Test Poem")
[ "$code" = "303" ] && pass "redirects on success" || fail "expected 303, got $code"

# 3. POST with file upload (no title)
echo -n "3. POST file only... "
echo "This is a test poem content.
With multiple lines." > "$TMPFILE"
sleep 0.1
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" -X POST "$BASE/poem/$USER/edit" \
	-F "file=@$TMPFILE")
sleep 0.3
[ "$code" = "303" ] && pass "redirects on success" || fail "expected 303, got $code"

# 4. Verify poem file was created
echo -n "4. Poem file created... "
[ -f "$POEM_DIR/$USER/pt_PT.html" ] && pass "poem file exists" || fail "poem file not found"

# 5. Verify poem content
echo -n "5. Poem content correct... "
content=$(cat "$POEM_DIR/$USER/pt_PT.html")
echo "$content" | grep -q "test poem content" && pass "content matches" || fail "content mismatch"

# 6. POST with both title and file
echo -n "6. POST title + file... "
echo "Updated poem content." > "$TMPFILE2"
sleep 0.1
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" -X POST "$BASE/poem/$USER/edit" \
	-F "title=Updated Title" -F "file=@$TMPFILE2")
sleep 0.3
[ "$code" = "303" ] && pass "redirects on success" || fail "expected 303, got $code"

# 7. Verify title file was written
echo -n "7. Title file created... "
[ -f "$POEM_DIR/$USER/title" ] && pass "title file exists" || fail "title file not found"

# 8. Verify title content
echo -n "8. Title content correct... "
title_content=$(cat "$POEM_DIR/$USER/title")
echo "$title_content" | grep -q "Updated Title" && pass "title matches" || fail "title mismatch: $title_content"

# 9. Verify updated poem content
echo -n "9. Updated poem content correct... "
content=$(cat "$POEM_DIR/$USER/pt_PT.html")
echo "$content" | grep -q "Updated poem content" && pass "content matches" || fail "content mismatch"

# 10. POST empty multipart (no fields) — should still redirect
echo -n "10. POST empty multipart... "
sleep 0.1
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" -X POST "$BASE/poem/$USER/edit" \
	-F "dummy=")
[ "$code" = "303" ] && pass "redirects on success" || fail "expected 303, got $code"

# 11. Unauthenticated edit should be 403
echo -n "11. Unauthenticated edit forbidden... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/poem/$USER/edit" \
	-F "title=Hacked")
[ "$code" = "403" ] && pass "403 forbidden" || fail "expected 403, got $code"

# 12. Delete poem
echo -n "12. Delete poem... "
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" -X POST "$BASE/poem/$USER/delete")
[ "$code" = "303" ] && pass "delete redirects" || fail "expected 303, got $code"

# 13. Verify poem directory removed
echo -n "13. Poem directory removed... "
[ ! -d "$POEM_DIR/$USER" ] && pass "directory gone" || fail "directory still exists"

# 14. Verify direct rendering via GET
echo -n "14. Verify detail GET... "
# Re-add a poem to test GET
echo "Direct render test" > "$TMPFILE"
curl -s -b "$COOKIE" -X POST "$BASE/poem/add" -F "title=direct_test" -F "file=@$TMPFILE" > /dev/null
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" "$BASE/poem/direct_test")
[ "$code" = "200" ] && pass "GET success" || fail "expected 200, got $code"

rm -f "$TMPFILE" "$TMPFILE2" "$COOKIE"
