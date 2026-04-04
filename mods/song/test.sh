#!/bin/sh
set -e

BASE="http://localhost:8080"
LOG="/tmp/site.log"
SONG_DIR="/home/quirinpa/site/items/song/items"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

TMPFILE="/tmp/song_test_$$"
TMPFILE2="/tmp/song_test2_$$"

mkdir -p "$SONG_DIR" || true

# 1. POST with wrong content-type
echo -n "1. POST wrong content-type... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/song/add" \
	-d "id=test&data=content")
[ "$code" = "415" ] && pass "415 for unsupported media type" || fail "expected 415, got $code"

# 2. POST missing id field
echo -n "2. POST missing id field... "
sleep 0.1
out=$(curl -s -X POST "$BASE/song/add" -F "data=test content")
echo "$out" | grep -q "Missing id" && pass "missing id detected" || fail "expected 'Missing id', got: $out"

# 3. POST missing data field
echo -n "3. POST missing data field... "
sleep 0.1
out=$(curl -s -X POST "$BASE/song/add" \
	-F "id=testchord")
echo "$out" | grep -q "Missing chord data" && pass "missing data detected" || fail "expected 'Missing chord data', got: $out"

# 4. POST empty data
echo -n "4. POST empty data... "
> "$TMPFILE2"
sleep 0.1
out=$(curl -s -X POST "$BASE/song/add" -F "id=testchord2" -F "data=@$TMPFILE2")
echo "$out" | grep -q "Missing chord data" && pass "empty data detected" || fail "expected 'Missing chord data', got: $out"

# 5. POST valid multipart with all fields
echo -n "5. POST valid multipart... "
echo "C       G       Am      F
Amazing Grace, how sweet the sound" > "$TMPFILE"
sleep 0.1
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/song/add" \
	-F "id=amazing_grace" \
	-F "title=Amazing Grace" \
	-F "type=Communion" \
	-F "data=@$TMPFILE")
[ "$code" = "303" ] && pass "redirects on success" || fail "expected 303, got $code"

# 6. Verify chord data file was created
echo -n "6. Chord data file created... "
[ -f "$SONG_DIR/amazing_grace/data.txt" ] && pass "data.txt exists" || fail "data.txt not found"

# 7. Verify chord content
echo -n "7. Chord content correct... "
content=$(cat "$SONG_DIR/amazing_grace/data.txt")
echo "$content" | grep -q "Amazing Grace" && pass "content matches" || fail "content mismatch"

# 8. Verify title file created
echo -n "8. Title file created... "
[ -f "$SONG_DIR/amazing_grace/title" ] && pass "title file exists" || fail "title file not found"

# 9. Verify type file created
echo -n "9. Type file created... "
[ -f "$SONG_DIR/amazing_grace/type" ] && pass "type file exists" || fail "type file not found"

# 10. POST without optional fields (only id and data)
echo -n "10. POST minimal fields... "
echo "D       A       Bm      G
Test chord content" > "$TMPFILE"
sleep 0.1
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/song/add" \
	-F "id=testchord" \
	-F "data=@$TMPFILE")
[ "$code" = "303" ] && pass "minimal upload works" || fail "expected 303, got $code"

# 11. Verify minimal chord was created
echo -n "11. Minimal chord created... "
[ -f "$SONG_DIR/testchord/data.txt" ] && pass "minimal chord exists" || fail "minimal chord not found"
