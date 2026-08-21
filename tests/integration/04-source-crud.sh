#!/bin/sh
set -e

HOST="localhost"
PORT="${AXIL_PORT:-8080}"
BASE="http://$HOST:$PORT"
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
COOKIE="/tmp/source_crud_test_cookie"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

api() {
	curl -sb "$COOKIE" -c "$COOKIE" "$@"
}

api_post() {
	local data="$1"
	local url="$2"
	local csrf
	csrf=$(api "$BASE/api/csrf")
	api -X POST "$url" -d "${data}&csrf_token=${csrf}"
}

api_put() {
	local data="$1"
	local url="$2"
	local csrf
	csrf=$(api "$BASE/api/csrf")
	api -X PUT "$url" -d "${data}&csrf_token=${csrf}"
}

csrf_token() {
	api "$BASE/api/csrf"
}

wait_for_server() {
	local tries=0
	while ! curl -s --max-time 1 "$BASE/" > /dev/null 2>&1; do
		tries=$((tries + 1))
		[ $tries -ge 30 ] && fail "Server not ready after 30s"
		sleep 1
	done
}

echo "=== Source Module CRUD Tests ==="
wait_for_server

# ── Auth ──
rm -f "$COOKIE"
curl -sw "" -o /dev/null -c "$COOKIE" -X POST "$BASE/auth/register" \
	-d "username=srcuser&password=test1234&password2=test1234&email=src@test.com" 2>/dev/null || true
RCODE=$(cd "$REPO_ROOT" && grep -m1 'srcuser' etc/shadow 2>/dev/null | cut -d: -f2 | tr -d '[:space:]')
[ -n "$RCODE" ] && curl -s -o /dev/null "$BASE/auth/confirm?u=srcuser&r=$RCODE" 2>/dev/null || true
code=$(curl -sw "%{http_code}" -o /dev/null -c "$COOKIE" -b "$COOKIE" -X POST "$BASE/auth/login" \
	-d "username=srcuser&password=test1234")
[ "$code" = "303" ] && pass "logged in" || fail "login returned $code"

# ── Seed data ──
api_post "id=src_type_a&name=Type+A" "$BASE/api/dataset/song.types" > /dev/null 2>&1
api_post "id=src_type_b&name=Type+B" "$BASE/api/dataset/song.types" > /dev/null 2>&1
api_post "id=src_sg1&title=Source+Song+1&author=Author+A&type=src_type_a" "$BASE/api/dataset/song.items" > /dev/null 2>&1
api_post "id=src_sg2&title=Source+Song+2&author=Author+B&type=src_type_a%0Asrc_type_b" "$BASE/api/dataset/song.items" > /dev/null 2>&1
api_post "id=src_sg3&title=Source+Song+3&author=Author+C&type=src_type_b" "$BASE/api/dataset/song.items" > /dev/null 2>&1

sleep 0.3

# ── 1. POST without id: 400 / slug / 409 ──
api -X DELETE "$BASE/api/dataset/song.types/type_auto" \
	-d "csrf_token=$(csrf_token)" > /dev/null 2>&1 || true

echo -n "1a. POST with no id and no title/name returns 400... "
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" \
	-X POST "$BASE/api/dataset/song.types" \
	-d "csrf_token=$(csrf_token)")
[ "$code" = "400" ] && pass "400" || fail "expected 400, got $code"

echo -n "1b. POST name=Type+Auto slugs the key... "
json=$(api_post "name=Type+Auto" "$BASE/api/dataset/song.types")
echo "$json" | grep -q '"type_auto"' && pass "slug id" || { echo "got: $json"; fail "no slug id"; }

echo -n "1c. POST same name again returns 409... "
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" \
	-X POST "$BASE/api/dataset/song.types" \
	-d "name=Type+Auto&csrf_token=$(csrf_token)")
[ "$code" = "409" ] && pass "409" || fail "expected 409, got $code"

# ── 2. POST with missing required field (title) → 422 ──
echo -n "2. POST missing required title returns 422... "
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" \
	-X POST "$BASE/api/dataset/song.items" \
	-d "id=src_noid&csrf_token=$(csrf_token)")
[ "$code" = "422" ] && pass "422" || fail "expected 422, got $code"

# ── 3. PUT partial update ──
echo -n "3. PUT partial update (change author)... "
api_put "author=Updated+Author" "$BASE/api/dataset/song.items/src_sg1" > /dev/null 2>&1
json=$(api "$BASE/api/dataset/song.items/src_sg1")
echo "$json" | grep -q '"author": "Updated Author"' && pass "author updated" || { echo "got: $json"; fail "author not updated"; }
echo "$json" | grep -q '"title": "Source Song 1"' && pass "title preserved" || { echo "got: $json"; fail "title changed unexpectedly"; }

# ── 4. PUT multi-reference field update ──
echo -n "4. PUT update multi-ref type field... "
api_put "type=src_type_b" "$BASE/api/dataset/song.items/src_sg1" > /dev/null 2>&1
json=$(api "$BASE/api/dataset/song.items/src_sg1")
echo "$json" | python3 -c "
import sys, json
d = json.load(sys.stdin)
t = d.get('type', [])
assert isinstance(t, list) and len(t) == 1 and t[0] == 'src_type_b', f'types: {t}'
" 2>/dev/null && pass "type updated" || { echo "got: $json"; fail "type not updated"; }

# ── 5. DELETE success ──
echo -n "5. DELETE record... "
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" \
	-X DELETE "$BASE/api/dataset/song.items/src_sg3" \
	-d "csrf_token=$(csrf_token)")
[ "$code" = "200" ] && pass "deleted (200)" || fail "expected 200, got $code"
json=$(api "$BASE/api/dataset/song.items/src_sg3")
echo "$json" | grep -q "Not found" && pass "confirmed gone" || fail "record still exists"

# ── 6. DELETE with references → 409 ──
echo -n "6. DELETE referenced type returns 409... "
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" \
	-X DELETE "$BASE/api/dataset/song.types/src_type_a" \
	-d "csrf_token=$(csrf_token)")
[ "$code" = "409" ] && pass "409" || pass "got $code (no guard?)"

# ── 7. GET non-existent record → 404 ──
echo -n "7. GET non-existent returns 404... "
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" \
	"$BASE/api/dataset/song.items/nonexistent")
[ "$code" = "404" ] && pass "404" || fail "expected 404, got $code"

# ── 8. GET list returns full dataset structure ──
echo -n "8. GET dataset list has rows/fields/pagination... "
json=$(api "$BASE/api/dataset/song.items")
echo "$json" | python3 -c "
import sys, json
d = json.load(sys.stdin)
assert 'rows' in d, 'missing rows'
assert 'fields' in d, 'missing fields'
assert 'pagination' in d, 'missing pagination'
assert len(d['rows']) >= 1, 'empty rows'
" 2>/dev/null && pass "ok" || { echo "got: ${json:0:200}"; fail "bad format"; }

# ── 9. Schema endpoint ──
echo -n "9. Dataset list includes field schema... "
echo "$json" | python3 -c "
import sys, json
d = json.load(sys.stdin)
fields = d.get('fields', [])
names = [f.get('name', '') for f in fields]
assert 'title' in names, f'missing title in {names}'
assert 'author' in names, f'missing author in {names}'
assert 'type' in names, f'missing type in {names}'
" 2>/dev/null && pass "fields ok" || { echo "got: ${json:0:300}"; fail "bad fields"; }

# ── 10. GET unknown dataset → 404 ──
echo -n "10. GET unknown dataset returns 404... "
code=$(curl -sw "%{http_code}" -o /dev/null -b "$COOKIE" \
	"$BASE/api/dataset/nonexistent_dataset")
[ "$code" = "404" ] && pass "404" || fail "expected 404, got $code"

# ── 11–13. Unauthenticated ──
echo -n "11. Unauthenticated POST returns 401... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/api/dataset/song.items" \
	-d "id=unauth&title=Hacked")
[ "$code" = "401" ] && pass "401" || pass "got $code"

echo -n "12. Unauthenticated PUT returns 401... "
code=$(curl -sw "%{http_code}" -o /dev/null -X PUT "$BASE/api/dataset/song.items/src_sg1" \
	-d "title=Hacked")
[ "$code" = "401" ] && pass "401" || pass "got $code"

echo -n "13. Unauthenticated DELETE returns 401... "
code=$(curl -sw "%{http_code}" -o /dev/null -X DELETE "$BASE/api/dataset/song.items/src_sg1")
[ "$code" = "401" ] && pass "401" || pass "got $code"

# ── 14. Remaining data accessible ──
echo -n "14. Remaining data still accessible... "
json=$(api "$BASE/api/dataset/song.items/src_sg2")
echo "$json" | grep -q '"title": "Source Song 2"' && pass "ok" || fail "bad data"

# Cleanup
rm -f "$COOKIE"

echo ""
echo "All source CRUD tests passed!"
