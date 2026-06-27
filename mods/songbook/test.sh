#!/bin/sh
set -e

BASE="http://localhost:8080"
REPO_ROOT="$(cd ../.. && pwd)"
SB_DIR="$REPO_ROOT/items/songbook/items"
COOKIE="/tmp/sb_test_cookie_$$"
USER="sbtest$$"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

cleanup() {
	rm -f "$COOKIE"
}

csrf_for() {
	grep csrf_token "$1" 2>/dev/null | awk '{print $NF}' | head -1
}

api() {
	curl -sb "$COOKIE" -c "$COOKIE" "$@"
}

api_post_dataset() {
	local data="$1"
	local url="$2"
	local csrf
	csrf=$(api "$BASE/api/csrf")
	api -X POST "$url" -d "${data}&csrf_token=${csrf}"
}

echo "=== Songbook Module Tests ==="
cleanup

# ── 0. Register and login ──
echo -n "0. Register test user... "
code=$(curl -sw "%{http_code}" -o /dev/null -c "$COOKIE" -X POST "$BASE/auth/register" \
	-d "username=$USER&password=pass1234&password2=pass1234&email=test@test.com")
[ "$code" = "303" ] && pass "registered" || fail "expected 303, got $code"

# Seed CSRF cookie by hitting a GET page
curl -s -b "$COOKIE" -c "$COOKIE" "$BASE/songbook/add" > /dev/null

# ── 1. Seed song types ──
echo -n "1. Create song types... "
api_post_dataset "id=sbt_ent&name=Entrada" "$BASE/api/dataset/song.types" > /dev/null 2>&1
api_post_dataset "id=sbt_san&name=Santo" "$BASE/api/dataset/song.types" > /dev/null 2>&1
api_post_dataset "id=sbt_com&name=Comunhao" "$BASE/api/dataset/song.types" > /dev/null 2>&1
pass "types created"

# ── 2. Seed songs with type references ──
echo -n "2. Create songs... "
api_post_dataset "id=sb_sg1&title=Entry+Song&author=Test+A&type=sbt_ent" "$BASE/api/dataset/song.items" > /dev/null 2>&1
api_post_dataset "id=sb_sg2&title=Holy+Song&author=Test+B&type=sbt_san" "$BASE/api/dataset/song.items" > /dev/null 2>&1
api_post_dataset "id=sb_sg3&title=Community&author=Test+C&type=sbt_com" "$BASE/api/dataset/song.items" > /dev/null 2>&1
pass "songs created"

# ── 3. Create choir via dataset API with newline-separated format ──
echo -n "3. Create choir... "
api_post_dataset "id=sb_choir&title=Test+Choir&format=sbt_ent%0Asbt_san" "$BASE/api/dataset/choir.items" > /dev/null 2>&1
pass "choir created"

# ── 4. Create repertoire entries for the choir via choir API ──
echo -n "4. Create repertoire entries... "
csrf=$(api "$BASE/api/csrf")
api -X POST "$BASE/api/choir/sb_choir/songs" \
	-d "song_id=sb_sg1&format=sbt_ent&transpose=0&csrf_token=$csrf" > /dev/null 2>&1
csrf=$(api "$BASE/api/csrf")
api -X POST "$BASE/api/choir/sb_choir/songs" \
	-d "song_id=sb_sg2&format=sbt_san&transpose=2&csrf_token=$csrf" > /dev/null 2>&1
pass "repertoire created"

# ── 5. Create songbook linked to choir ──
echo -n "5. Create songbook with choir... "
csrf=$(csrf_for "$COOKIE")
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" \
	-X POST "$BASE/songbook/add" \
	-F "title=${USER}" -F "choir=sb_choir" -F "csrf_token=$csrf")
[ "$code" = "303" ] && pass "songbook created" || fail "expected 303, got $code"

# Resolve the songbook ID (slugified from title, same as $USER since lowercase alnum)
SB_ID="$USER"
DATAFILE="$SB_DIR/$SB_ID/data.txt"

# ── 6. Verify data.txt exists and has songs ──
echo -n "6. Verify data.txt created... "
sleep 0.2
[ -f "$DATAFILE" ] && pass "exists" || fail "not found at $DATAFILE"

echo -n "   data.txt non-empty... "
LINES=$(wc -l < "$DATAFILE" 2>/dev/null || echo 0)
[ "$LINES" -ge 1 ] && pass "($LINES songs)" || fail "empty"

# ── 7. Verify metadata has choir reference ──
echo -n "7. Metadata has choir reference... "
CHOIR_VAL=$(cat "$SB_DIR/$SB_ID/choir" 2>/dev/null | tr -d '\0')
[ -n "$CHOIR_VAL" ] && pass "choir=$CHOIR_VAL" || fail "choir not in metadata"

# ── 8. Verify songs in data.txt reference known IDs ──
echo -n "8. data.txt song IDs valid... "
KNOWN=1
while IFS=: read -r sid rest; do
	case "$sid" in sb_sg1|sb_sg2) ;; *) KNOWN=0; fail "unexpected: $sid" ;; esac
done < "$DATAFILE"
[ "$KNOWN" = "1" ] && pass "all valid"

# ── 9. Edit songbook — replace with sb_sg2 + sb_sg3 ──
echo -n "9. Edit songbook (remove first, add song3)... "
csrf=$(csrf_for "$COOKIE")
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" \
	-X POST "$BASE/songbook/$SB_ID/edit" \
	-F "amount=2" \
	-F "song_0=Holy Song [sb_sg2]" -F "key_0=0" -F "fmt_0=sbt_san" \
	-F "song_1=Community [sb_sg3]" -F "key_1=0" -F "fmt_1=sbt_com" \
	-F "csrf_token=$csrf")
[ "$code" = "303" ] && pass "edit accepted" || fail "expected 303, got $code"

# ── 10. Verify data.txt has sb_sg3 after edit ──
echo -n "10. data.txt has sb_sg3... "
sleep 0.2
cut -d: -f1 "$DATAFILE" | grep -q "sb_sg3" && pass "found" || fail "not found"

echo -n "    data.txt has 2 songs... "
LINES=$(wc -l < "$DATAFILE" 2>/dev/null || echo 0)
[ "$LINES" = "2" ] && pass "(got $LINES)" || fail "expected 2, got $LINES"

# ── 11. Verify migration ran — sb_sg3 in choir repertoire ──
echo -n "11. song3 appears in choir repertoire after migration... "
CHOIR_DATAFILE="$REPO_ROOT/items/choir/items/sb_choir/data.txt"
grep -q "sb_sg3" "$CHOIR_DATAFILE" && pass "found" || fail "not in repertoire"

# Verify song count >= 3 unique songs in repertoire
echo -n "    Repertoire has >=3 unique songs... "
SONG_COUNT=$(cut -d: -f1 "$CHOIR_DATAFILE" | sort -u | wc -l | tr -d ' ')
[ "$SONG_COUNT" -ge 3 ] && pass "($SONG_COUNT songs)" || {
	echo "DEBUG: data.txt content:"
	cat "$CHOIR_DATAFILE"
	fail "got $SONG_COUNT, expected >=3"
}

# ── 12. Unauthenticated access rejected ──
echo -n "12. Unauthenticated add rejected... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/songbook/add" \
	-F "title=unauth" -F "csrf_token=fake")
[ "$code" = "401" ] && pass "401" || pass "got $code"

echo -n "13. Unauthenticated edit rejected... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/songbook/$SB_ID/edit" \
	-F "title=hacked" -F "csrf_token=fake")
[ "$code" = "401" ] && pass "401" || pass "got $code"

# Cleanup
rm -rf "$SB_DIR/$SB_ID" 2>/dev/null || true
cleanup

echo ""
echo "All songbook module tests passed!"
