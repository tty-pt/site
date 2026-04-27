#!/bin/sh
set -eu

HOST="${NDC_HOST:-localhost}"
PORT="${NDC_PORT:-8080}"
BASE="http://$HOST:$PORT"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

check_redirect() {
	route=$1
	expected=$2
	expected_code=$3
	desc=$4
	headers=$(mktemp)

	code=$(curl -sS -D "$headers" -o /dev/null -w "%{http_code}" "$BASE$route" || true)
	location=$(sed -n 's/^Location: //p' "$headers" | tr -d '\r' | tail -n 1)
	rm -f "$headers"

	[ "$code" = "$expected_code" ] || fail "$desc: expected $expected_code, got $code"
	[ "$location" = "$expected" ] || fail "$desc: expected Location $expected, got $location"
	pass "$desc"
}

echo "=== Redirect Module Tests ==="

check_redirect "/sb" "/songbook" "303" "songbook list redirect"
check_redirect "/sb/" "/songbook/" "303" "songbook trailing slash redirect"
check_redirect "/sb/add" "/songbook/add" "303" "songbook add redirect"
check_redirect "/sb/test-book/edit?foo=bar" "/songbook/test-book/edit?foo=bar" "303" "songbook edit redirect"
check_redirect "/chords/amazing_grace" "/song/amazing_grace" "301" "song detail redirect"
check_redirect "/chords/amazing_grace?t=2&b=1" "/song/amazing_grace?t=2&b=1" "301" "song query redirect"

echo ""
echo "All redirect module tests passed!"
