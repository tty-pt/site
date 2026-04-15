#!/bin/sh
set -e

BASE="http://localhost:8080"
SONG_DIR="/home/quirinpa/site/items/song/items"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

mkdir -p "$SONG_DIR/amazing_grace" "$SONG_DIR/testchord" || true

# 1. POST with wrong content-type (song edit uses url-encoded; multipart → 415 not applicable)
# The edit handler uses call_query_parse which accepts any body, so test that it works
echo -n "1. POST url-encoded edit... "
mkdir -p "$SONG_DIR/amazing_grace"
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/song/amazing_grace/edit" \
	--data-urlencode "title=Amazing Grace" \
	--data-urlencode "data=C G Am F
Amazing Grace, how sweet the sound")
[ "$code" = "303" ] && pass "redirects on success" || fail "expected 303, got $code"

# 2. Verify title file written
echo -n "2. Title file created... "
[ -f "$SONG_DIR/amazing_grace/title" ] && pass "title file exists" || fail "title file not found"

# 3. Verify title content
echo -n "3. Title content correct... "
content=$(cat "$SONG_DIR/amazing_grace/title")
echo "$content" | grep -q "Amazing Grace" && pass "title matches" || fail "title mismatch: $content"

# 4. Verify data file written
echo -n "4. Data file created... "
[ -f "$SONG_DIR/amazing_grace/data.txt" ] && pass "data.txt exists" || fail "data.txt not found"

# 5. Verify chord content
echo -n "5. Chord content correct... "
content=$(cat "$SONG_DIR/amazing_grace/data.txt")
echo "$content" | grep -q "Amazing Grace" && pass "content matches" || fail "content mismatch"

# 6. POST with type field
echo -n "6. POST with type... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/song/amazing_grace/edit" \
	--data-urlencode "type=Communion")
[ "$code" = "303" ] && pass "redirects on success" || fail "expected 303, got $code"

# 7. Verify type file created
echo -n "7. Type file created... "
[ -f "$SONG_DIR/amazing_grace/type" ] && pass "type file exists" || fail "type file not found"

# 8. Verify type content
echo -n "8. Type content correct... "
content=$(cat "$SONG_DIR/amazing_grace/type")
echo "$content" | grep -q "Communion" && pass "type matches" || fail "type mismatch: $content"

# 9. POST minimal (empty body) — all fields optional, should still redirect
echo -n "9. POST minimal (empty)... "
mkdir -p "$SONG_DIR/testchord"
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/song/testchord/edit" \
	-d "")
[ "$code" = "303" ] && pass "redirects on success" || fail "expected 303, got $code"

# 10. POST with yt and audio fields
echo -n "10. POST yt + audio fields... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/song/amazing_grace/edit" \
	--data-urlencode "yt=dQw4w9WgXcQ" \
	--data-urlencode "audio=amazing_grace.mp3")
[ "$code" = "303" ] && pass "redirects on success" || fail "expected 303, got $code"

# 11. Verify yt file written
echo -n "11. YT file created... "
[ -f "$SONG_DIR/amazing_grace/yt" ] && pass "yt file exists" || fail "yt file not found"
