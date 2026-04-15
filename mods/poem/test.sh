#!/bin/sh
set -e

BASE="http://localhost:8080"
POEM_DIR="/home/quirinpa/site/items/poem/items"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

TMPFILE="/tmp/poem_test_$$"
TMPFILE2="/tmp/poem_test2_$$"

mkdir -p "$POEM_DIR/testpoem"

# 1. POST with wrong content-type
echo -n "1. POST wrong content-type... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/poem/testpoem/edit" \
	-d "title=test")
[ "$code" = "415" ] && pass "415 for unsupported media type" || fail "expected 415, got $code"

# 2. POST with only title (no file) — both fields are optional
echo -n "2. POST title only... "
sleep 0.1
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/poem/testpoem/edit" \
	-F "title=My Test Poem")
[ "$code" = "303" ] && pass "redirects on success" || fail "expected 303, got $code"

# 3. POST with file upload (no title)
echo -n "3. POST file only... "
echo "This is a test poem content.
With multiple lines." > "$TMPFILE"
sleep 0.1
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/poem/testpoem/edit" \
	-F "file=@$TMPFILE")
sleep 0.3
[ "$code" = "303" ] && pass "redirects on success" || fail "expected 303, got $code"

# 4. Verify poem file was created
echo -n "4. Poem file created... "
[ -f "$POEM_DIR/testpoem/pt_PT.html" ] && pass "poem file exists" || fail "poem file not found"

# 5. Verify poem content
echo -n "5. Poem content correct... "
content=$(cat "$POEM_DIR/testpoem/pt_PT.html")
echo "$content" | grep -q "test poem content" && pass "content matches" || fail "content mismatch"

# 6. POST with both title and file
echo -n "6. POST title + file... "
echo "Updated poem content." > "$TMPFILE2"
sleep 0.1
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/poem/testpoem/edit" \
	-F "title=Updated Title" -F "file=@$TMPFILE2")
sleep 0.3
[ "$code" = "303" ] && pass "redirects on success" || fail "expected 303, got $code"

# 7. Verify title file was written
echo -n "7. Title file created... "
[ -f "$POEM_DIR/testpoem/title" ] && pass "title file exists" || fail "title file not found"

# 8. Verify title content
echo -n "8. Title content correct... "
title_content=$(cat "$POEM_DIR/testpoem/title")
echo "$title_content" | grep -q "Updated Title" && pass "title matches" || fail "title mismatch: $title_content"

# 9. Verify updated poem content
echo -n "9. Updated poem content correct... "
content=$(cat "$POEM_DIR/testpoem/pt_PT.html")
echo "$content" | grep -q "Updated poem content" && pass "content matches" || fail "content mismatch"

# 10. POST empty multipart (no fields) — should still redirect
echo -n "10. POST empty multipart... "
sleep 0.1
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/poem/testpoem/edit" \
	-F "dummy=")
[ "$code" = "303" ] && pass "redirects on success" || fail "expected 303, got $code"
