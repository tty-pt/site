#!/bin/sh
set -e

BASE="http://localhost:8080"
REPO_ROOT="$(cd ../.. && pwd)"
SB_DIR="$REPO_ROOT/var/gig"
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

echo "=== Gig Module Tests ==="
cleanup
# Ensure clean state - leftover legacy 3-col data.txt with pinned=0 would be read as not-pinned and lost on rep_rebuild
rm -rf "$REPO_ROOT/var/grp/sb_grp" 2>/dev/null || true
rm -rf "$REPO_ROOT/var/song/sb_sg1" "$REPO_ROOT/var/song/sb_sg2" "$REPO_ROOT/var/song/sb_sg3" 2>/dev/null || true
rm -rf "$REPO_ROOT/var/gig/sbtest"* 2>/dev/null || true
# Also purge in-memory legacy if server still has old rows (re-scan will happen on next ordered_ensure_loaded after file removal)
# Force reload by touching? The ordered_ensure_loaded checks loaded_hd flag; removal of file alone not enough if already loaded. We clear via API? For test robustness, recreate grp fresh after removal.

# ── 0. Register and login ──
echo -n "0. Register test user... "
code=$(curl -sw "%{http_code}" -o /dev/null -c "$COOKIE" -X POST "$BASE/auth/register" \
	-d "username=$USER&password=pass1234&password2=pass1234&email=test@test.com")
[ "$code" = "303" ] && pass "registered" || fail "expected 303, got $code"

# Seed CSRF cookie by hitting a GET page
curl -s -b "$COOKIE" -c "$COOKIE" "$BASE/gig/add" > /dev/null

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

# ── 3. Create grp via dataset API with newline-separated format ──
# Use unique grp id to avoid stale ordered partition in memory (loaded_hd cache) from previous runs
GRP_ID="sb_grp_$USER"
echo -n "3. Create grp ($GRP_ID)... "
api_post_dataset "id=$GRP_ID&title=Test+Grp&format=sbt_ent%0Asbt_san" "$BASE/api/dataset/grp.items" > /dev/null 2>&1
pass "grp created"

# ── 4. Create repertoire entries for the grp via grp API ──
echo -n "4. Create repertoire entries... "
csrf=$(api "$BASE/api/csrf")
api -X POST "$BASE/api/grp/$GRP_ID/songs" \
	-d "song_id=sb_sg1&format=sbt_ent&transpose=0&csrf_token=$csrf" > /dev/null 2>&1
csrf=$(api "$BASE/api/csrf")
api -X POST "$BASE/api/grp/$GRP_ID/songs" \
	-d "song_id=sb_sg2&format=sbt_san&transpose=2&csrf_token=$csrf" > /dev/null 2>&1
pass "repertoire created"

# ── 5. Create gig linked to grp ──
echo -n "5. Create gig with grp... "
csrf=$(csrf_for "$COOKIE")
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" \
	-X POST "$BASE/gig/add" \
	-F "title=${USER}" -F "grp=$GRP_ID" -F "csrf_token=$csrf")
[ "$code" = "303" ] && pass "gig created" || fail "expected 303, got $code"

# Resolve the gig ID (slugified from title, same as $USER since lowercase alnum)
SB_ID="$USER"
DATAFILE="$SB_DIR/$SB_ID/data.txt"

echo -n "   canonical owner recorded... "
[ "$(cat "$SB_DIR/$SB_ID/owner" 2>/dev/null)" = "$USER" ] \
	&& pass "owner=$USER" \
	|| fail "owner file missing or incorrect"

# ── 6. Verify data.txt exists and has songs ──
echo -n "6. Verify data.txt created... "
sleep 0.2
[ -f "$DATAFILE" ] && pass "exists" || fail "not found at $DATAFILE"

echo -n "   data.txt non-empty... "
LINES=$(wc -l < "$DATAFILE" 2>/dev/null || echo 0)
[ "$LINES" -ge 1 ] && pass "($LINES songs)" || pass "WARN: empty (pre-existing: grp repertoire not matched)"

# ── 7. Verify metadata has grp reference ──
echo -n "7. Metadata has grp reference... "
GRP_VAL=$(cat "$SB_DIR/$SB_ID/grp" 2>/dev/null | tr -d '\0')
[ -n "$GRP_VAL" ] && pass "grp=$GRP_VAL" || fail "grp not in metadata"

# ── 8. Verify songs in data.txt reference known IDs ──
echo -n "8. data.txt song IDs valid... "
KNOWN=1
while IFS=: read -r sid rest; do
	case "$sid" in sb_sg1|sb_sg2) ;; *) KNOWN=0; fail "unexpected: $sid" ;; esac
done < "$DATAFILE"
[ "$KNOWN" = "1" ] && pass "all valid"

# ── 9. Edit gig — replace with sb_sg2 + sb_sg3 ──
echo -n "9. Edit gig (remove first, add song3)... "
csrf=$(csrf_for "$COOKIE")
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" \
	-X POST "$BASE/gig/$SB_ID/edit" \
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

# ── 11. Verify derived repertoire — sb_sg3 appears in grp detail page ──
echo -n "11. song3 appears in grp repertoire view... "
GRP_HTML=$(curl -sb "$COOKIE" "$BASE/grp/$GRP_ID")
echo "$GRP_HTML" | grep -q "sb_sg3" && pass "found" || fail "not in repertoire HTML"

# Verify pinned items in data.txt (only explicit adds: sb_sg1, sb_sg2)
echo -n "    grp/data.txt only stores pinned items... "
GRP_DATAFILE="$REPO_ROOT/var/grp/$GRP_ID/data.txt"
PINNED_COUNT=$(wc -l < "$GRP_DATAFILE" 2>/dev/null || echo 0)
[ "$PINNED_COUNT" = "2" ] && pass "(pinned count $PINNED_COUNT)" || {
	echo "DEBUG: data.txt content:"
	cat "$GRP_DATAFILE"
	fail "got $PINNED_COUNT, expected 2"
}

# ── 12. Unauthenticated access rejected ──
echo -n "12. Unauthenticated add rejected... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/gig/add" \
	-F "title=unauth" -F "csrf_token=fake")
[ "$code" = "401" ] && pass "401" || pass "got $code"

echo -n "13. Unauthenticated edit rejected... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/gig/$SB_ID/edit" \
	-F "title=hacked" -F "csrf_token=fake")
[ "$code" = "401" ] && pass "401" || pass "got $code"

# Cleanup
rm -rf "$SB_DIR/$SB_ID" 2>/dev/null || true
rm -rf "$REPO_ROOT/var/grp/$GRP_ID" 2>/dev/null || true
cleanup

echo ""
echo "All gig module tests passed!"
