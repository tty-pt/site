#!/bin/sh
set -e

HOST="localhost"
PORT="${AXIL_PORT:-8080}"
BASE="http://$HOST:$PORT"
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
COOKIE="/tmp/generic_json_test_cookie"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

api() {
	curl -sb "$COOKIE" -c "$COOKIE" "$@"
}

api_post() {
	local data="$1"
	local csrf
	csrf=$(api "$BASE/api/csrf")
	api -X POST "$2" -d "${data}&csrf_token=${csrf}"
}

wait_for_server() {
	local tries=0
	while ! curl -s --max-time 1 "$BASE/" > /dev/null 2>&1; do
		tries=$((tries + 1))
		[ $tries -ge 30 ] && fail "Server not ready after 30s"
		sleep 1
	done
}

echo "=== Generic Record-to-JSON Tests ==="
wait_for_server

# ── Cleanup previous test fixtures ──
rm -rf "$REPO_ROOT"/var/grp/gj_* "$REPO_ROOT"/var/song/gj_* "$REPO_ROOT"/var/gig/gj_* "$REPO_ROOT"/var/song.types/gj_* 2>/dev/null || true

# ── Auth ──
rm -f "$COOKIE"
curl -sw "" -o /dev/null -c "$COOKIE" -X POST "$BASE/auth/register" \
	-d "username=jsonuser&password=test1234&password2=test1234&email=json@test.com" 2>/dev/null || true

RCODE=$(cd "$REPO_ROOT" && grep -m1 'jsonuser' etc/shadow 2>/dev/null | cut -d: -f2 | tr -d '[:space:]')
[ -n "$RCODE" ] && curl -s -o /dev/null "$BASE/auth/confirm?u=jsonuser&r=$RCODE" 2>/dev/null || true

code=$(curl -sw "%{http_code}" -o /dev/null -c "$COOKIE" -b "$COOKIE" -X POST "$BASE/auth/login" \
	-d "username=jsonuser&password=test1234")
[ "$code" = "303" ] && pass "logged in" || fail "login returned $code"

# ── Create test data via API ──
api_post "id=gj_type_c&name=Communion" "$BASE/api/dataset/song.types" > /dev/null 2>&1
api_post "id=gj_type_e&name=Entry" "$BASE/api/dataset/song.types" > /dev/null 2>&1

api_post "id=gj_song1&title=Amazing+Grace&author=John+Newton&yt=dQw4w9WgXcQ&type=gj_type_c%0Agj_type_e" "$BASE/api/dataset/song.items" > /dev/null 2>&1
api_post "id=gj_song2&title=Holy+Holy+Holy&author=Reginald+Heber&type=gj_type_c" "$BASE/api/dataset/song.items" > /dev/null 2>&1

api_post "id=gj_grp&title=Test+Grp&format=entrada,santo" "$BASE/api/dataset/grp.items" > /dev/null 2>&1
api_post "id=gj_gig1&title=Test+Gig&grp=gj_grp" "$BASE/api/dataset/gig.items" > /dev/null 2>&1

api_post "id=gj_repo1&song=gj_song1&transpose=0&format=entrada&grp=gj_grp" "$BASE/api/dataset/grp.songs/gj_grp/ordered" > /dev/null 2>&1
api_post "id=gj_repo2&song=gj_song2&transpose=2&format=santo&grp=gj_grp" "$BASE/api/dataset/grp.songs/gj_grp/ordered" > /dev/null 2>&1

sleep 1

# ── Tests ──

echo -n "1. STRING fields (grp.title)... "
json=$(api "$BASE/api/dataset/grp.items/gj_grp")
echo "$json" | grep -qE '"title":\s*"Test Grp"' && pass || fail "$json"

echo -n "2. REFERENCE field in ordered partition (song)... "
json=$(api "$BASE/api/dataset/grp.songs/gj_grp/ordered")
echo "$json" | grep -qE '"song":\s*"gj_song1"' && pass || fail "$json"

echo -n "3. FIELD in ordered partition (format)... "
echo "$json" | grep -qE '"format":\s*"entrada"' && pass || fail "$json"

echo -n "4. INVERSE field (grp.gigs populated)... "
json=$(api "$BASE/api/dataset/grp.items/gj_grp")
echo "$json" | grep -qE '"gigs":\s*\[\s*"gj_gig1"\s*\]' && pass || fail "$json"

echo -n "5. Song STRING fields... "
json=$(api "$BASE/api/dataset/song.items/gj_song1")
echo "$json" | grep -qE '"title":\s*"Amazing Grace"' && pass || fail "$json"

echo -n "7. Song author field... "
echo "$json" | grep -qE '"author":\s*"John Newton"' && pass || fail "$json"

echo -n "8. MULTI_REFERENCE (song.type field)... "
echo "$json" | grep -qE 'Communion|gj_type_c' && pass || fail "$json"

echo -n "9. All expected fields present... "
echo "$json" | grep -q '"id"' && echo "$json" | grep -q '"title"' && echo "$json" | grep -q '"author"' && pass || fail "$json"

echo -n "10. NULLABLE_STRING (song.yt)... "
echo "$json" | grep -qE '"yt":\s*"dQw4w9WgXcQ"' && pass || fail "$json"

echo ""
echo "All generic record-to-JSON tests passed!"
