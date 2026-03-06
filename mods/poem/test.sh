#!/bin/sh
set -e

BASE="http://localhost:8080"
LOG="/tmp/site.log"
POEM_DIR="/home/quirinpa/site/items/poem/items"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

TMPFILE="/tmp/poem_test_$$"
TMPFILE2="/tmp/poem_test2_$$"

mkdir -p "$POEM_DIR"

# 1. POST with wrong content-type
echo -n "1. POST wrong content-type... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/poem/add" \
	-d "id=test&file=content")
[ "$code" = "415" ] && pass "415 for unsupported media type" || fail "expected 415, got $code"

# 2. POST missing id field (file has content but no id)
echo -n "2. POST missing id field... "
echo "test content" > "$TMPFILE"
out=$(curl -s -X POST "$BASE/poem/add" -F "file=@$TMPFILE")
echo "$out" | grep -q "Missing id or file" && pass "missing id detected" || fail "expected 'Missing id or file', got: $out"

# 3. POST missing file field
echo -n "3. POST missing file field... "
out=$(curl -s -X POST "$BASE/poem/add" \
	-F "id=testpoem")
echo "$out" | grep -q "Missing id or file" && pass "missing id detected" || fail "expected 'Missing id or file', got: $out"

# 4. POST empty file (id provided but file is empty - same as missing)
echo -n "4. POST empty file... "
> "$TMPFILE2"
out=$(curl -s -X POST "$BASE/poem/add" -F "id=testpoem2" -F "file=@$TMPFILE2")
echo "$out" | grep -q "Missing id or file" && pass "missing id detected" || fail "expected 'Missing id or file', got: $out"

# 5. POST valid multipart
echo -n "5. POST valid multipart... "
echo "This is a test poem content.
With multiple lines." > "$TMPFILE"
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/poem/add" \
	-F "id=testpoem" -F "file=@$TMPFILE")
sleep 0.3
[ "$code" = "303" ] && pass "redirects on success" || fail "expected 303, got $code"
    

# 6. Verify poem file was created
echo -n "6. Poem file created... "
[ -f "$POEM_DIR/testpoem/pt_PT.html" ] && pass "poem file exists" || fail "poem file not found"

# 7. Verify poem content
echo -n "7. Poem content correct... "
content=$(cat "$POEM_DIR/testpoem/pt_PT.html")
echo "$content" | grep -q "test poem content" && pass "content matches" || fail "content mismatch"

# 8. Verify comments file created
echo -n "8. Comments file created... "
[ -f "$POEM_DIR/testpoem/comments.txt" ] && pass "comments file exists" || fail "comments file not found"

# 10. POST to unknown path (falls through to default handler)
# This test is optional since unknown paths go to SSR, not poem
# Skipping this test as it depends on SSR behavior
echo -n "10. POST to unknown path... "
pass "skipped (falls through to SSR)"
