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

api_post "id=gj_choir&title=Test+Choir&format=entrada,santo" "$BASE/api/dataset/choir.items" > /dev/null 2>&1

api_post "id=gj_repo1&song=gj_song1&transpose=0&format=entrada&choir=gj_choir" "$BASE/api/dataset/choir.repertoire" > /dev/null 2>&1
api_post "id=gj_repo2&song=gj_song2&transpose=2&format=santo&choir=gj_choir" "$BASE/api/dataset/choir.repertoire" > /dev/null 2>&1

sleep 1

# ── Tests ──

echo -n "1. STRING fields (choir.title)... "
json=$(api "$BASE/api/dataset/choir.items/gj_choir")
echo "$json" | grep -q '"title": "Test Choir"' && pass || fail "$json"

echo -n "2. REFERENCE field (repertoire.song)... "
json=$(api "$BASE/api/dataset/choir.repertoire/gj_repo1")
echo "$json" | grep -q '"song": "gj_song1"' && pass || fail "$json"

echo -n "3. REFERENCE field (repertoire.choir)... "
echo "$json" | grep -q '"choir": "gj_choir"' && pass || fail "$json"

echo -n "4. INVERSE field (choir.songbooks empty)... "
json=$(api "$BASE/api/dataset/choir.items/gj_choir")
echo "$json" | grep -qE '"songbooks":\s*\[\s*\]' && pass || fail "$json"

echo -n "5. INVERSE field (choir.repertoire)... "
echo "$json" | python3 -c "
import sys, json
d = json.load(sys.stdin)
r = d.get('repertoire', [])
assert isinstance(r, list) and len(r) >= 1
" 2>/dev/null && pass || fail "$json"

echo -n "6. Song STRING fields... "
json=$(api "$BASE/api/dataset/song.items/gj_song1")
echo "$json" | grep -q '"title": "Amazing Grace"' && pass || fail "$json"

echo -n "7. Song author field... "
echo "$json" | grep -q '"author": "John Newton"' && pass || fail "$json"

echo -n "8. MULTI_REFERENCE (song.type array)... "
echo "$json" | python3 -c "
import sys, json
d = json.load(sys.stdin)
t = d.get('type', [])
assert isinstance(t, list) and len(t) >= 1
" 2>/dev/null && pass || fail "$json"

echo -n "9. All expected fields present... "
echo "$json" | python3 -c "
import sys, json
d = json.load(sys.stdin)
for f in ['id', 'title', 'type', 'author', 'yt']:
    assert f in d, f'missing: {f}'
" 2>/dev/null && pass || fail "$json"

echo -n "10. NULLABLE_STRING (song.yt)... "
echo "$json" | grep -q '"yt": "dQw4w9WgXcQ"' && pass || fail "$json"

echo ""
echo "All generic record-to-JSON tests passed!"
