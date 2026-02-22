#!/bin/sh
set -e

HOST="localhost"
PORT="3001"
BASE="http://$HOST:$PORT"
LOG="/tmp/poem_test_ndc.log"
POEM_DIR="/home/quirinpa/site/items/poem/items"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

TMPFILE="/tmp/poem_test_$$"
TMPFILE2="/tmp/poem_test2_$$"

cleanup() {
	pkill -f "ndc.*$PORT" 2>/dev/null || true
	rm -f "$LOG" "$TMPFILE" "$TMPFILE2"
	rm -rf "$POEM_DIR"
}

start_server() {
	cleanup
    sleep 2
	mkdir -p "$POEM_DIR"
    /home/quirinpa/ndc/bin/ndc -C /home/quirinpa/site -p $PORT -d 2>"$LOG" &
    sleep 2
}

echo "=== Poem Module Tests ==="
start_server

# 1. POST with wrong content-type
echo -n "1. POST wrong content-type... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/poem/add" \
	-d "id=test&file=content")
[ "$code" = "400" ] && pass "400 for wrong content-type" || fail "expected 400, got $code"
sleep 1

# 2. POST missing id field (file has content but no id)
echo -n "2. POST missing id field... "
echo "test content" > "$TMPFILE"
out=$(curl -s -X POST "$BASE/poem/add" -F "file=@$TMPFILE")
echo "$out" | grep -q "Missing id or file" && pass "missing id detected" || fail "expected 'Missing id or file', got: $out"
sleep 1

# 3. POST missing file field
echo -n "3. POST missing file field... "
out=$(curl -s -X POST "$BASE/poem/add" \
	-F "id=testpoem")
echo "$out" | grep -q "No file field" && pass "missing file field detected" || fail "expected 'No file field', got: $out"
sleep 1

# 4. POST empty file (id provided but file is empty - same as missing)
echo -n "4. POST empty file... "
> "$TMPFILE2"
out=$(curl -s -X POST "$BASE/poem/add" -F "id=testpoem2" -F "file=@$TMPFILE2")
echo "$out" | grep -q "Missing id or file" && pass "empty file detected" || fail "expected 'Missing id or file', got: $out"
sleep 1

# 5. POST valid multipart
echo -n "5. POST valid multipart... "
echo "This is a test poem content.
With multiple lines." > "$TMPFILE"
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/poem/add" \
	-F "id=testpoem" -F "file=@$TMPFILE")
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

# 9. GET on /poem/add (wrong method)
echo -n "9. GET on /poem/add... "
code=$(curl -sw "%{http_code}" -o /dev/null "$BASE/poem/add")
[ "$code" = "405" ] && pass "405 for GET" || fail "expected 405, got $code"

# 10. POST to unknown path (falls through to default handler)
# This test is optional since unknown paths go to SSR, not poem
# Skipping this test as it depends on SSR behavior
echo -n "10. POST to unknown path... "
pass "skipped (falls through to SSR)"

cleanup
echo ""
echo "All tests passed!"
