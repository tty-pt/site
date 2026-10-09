#!/bin/sh
set -e

HOST="localhost"
PORT="${AXIL_PORT:-8080}"
BASE="http://$HOST:$PORT"

# Environment setup
SITE_DIR="${SITE_DIR:-$(pwd)}"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

TMPFILE="/tmp/home_buttons_test_$$"

cleanup() {
	rm -f "$TMPFILE"
}
trap cleanup EXIT HUP INT TERM

wait_for_server() {
	local tries=0
	while ! curl -s --max-time 1 "$BASE/" > /dev/null 2>&1; do
		tries=$((tries + 1))
		[ $tries -ge 30 ] && fail "Server not ready after 30s"
		sleep 1
	done
}

echo "=== Home Module Buttons Integration Tests ==="
wait_for_server

# The homepage renders one button per registered index module
# (mods/index/ux/home.c). Every module registers its button once from its
# own xy_install; a module installed twice (e.g. the site list plus a second
# loader reading the same list) renders its button twice. Each module button
# must appear exactly once. Other buttons on the page (login/register) are
# not modules and are deliberately not counted.
curl -s --max-time 5 "$BASE/" -o "$TMPFILE" \
	|| fail "GET / failed"

for mod in poem song grp gig; do
	n=$(grep -o "<a [^>]*href=\"/$mod/\"[^>]*>" "$TMPFILE" | grep -c "btn" || true)
	[ "$n" -eq 1 ] \
		|| fail "expected exactly 1 button for /$mod/, got $n"
	pass "/$mod/ button present exactly once"
done

pass "homepage shows each module button exactly once"
