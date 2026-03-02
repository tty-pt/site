#!/bin/sh
set -e

HOST="localhost"
PORT="3002"
BASE="http://$HOST:$PORT"
LOG="/tmp/poem_listing_test_ndc.log"
DENO_LOG="/tmp/poem_listing_test_deno.log"
POEM_DIR="/home/quirinpa/site/items/poem/items"
TMPFILE1="/tmp/poem_listing_test_1_$$"
TMPFILE2="/tmp/poem_listing_test_2_$$"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

cleanup() {
	# Kill processes by port number to be more specific
	fuser -k $PORT/tcp >/dev/null 2>&1 || true
	pkill -f "deno.*server.ts" 2>/dev/null || true
	sleep 1
	rm -rf "$POEM_DIR"
	rm -f "$LOG" "$DENO_LOG" "$TMPFILE1" "$TMPFILE2"
}

start_server() {
	cleanup
	sleep 2
	mkdir -p "$POEM_DIR"
	
	# Start Deno SSR server
	cd /home/quirinpa/site/mods/ssr
	/home/quirinpa/.deno/bin/deno run --allow-net --allow-read --allow-env server.ts > "$DENO_LOG" 2>&1 &
	deno_pid=$!
	cd - > /dev/null
	sleep 2
	
	# Start ndc
	LD_LIBRARY_PATH=/home/quirinpa/ndc/lib:/home/quirinpa/qmap/lib \
		/home/quirinpa/ndc/bin/ndc -C /home/quirinpa/site -p $PORT -d 2>"$LOG" &
	sleep 3
}

wait_for_server() {
	max_attempts=20
	attempt=0
	while [ $attempt -lt $max_attempts ]; do
		if curl -s "$BASE/poem/" > /dev/null 2>&1; then
			# Extra verification - check we can actually see the page
			test_out=$(curl -s "$BASE/poem/" 2>&1)
			if echo "$test_out" | grep -q "poems\|Add Poem"; then
				return 0
			fi
		fi
		attempt=$((attempt + 1))
		sleep 1
	done
	return 1
}

echo "=== Poem Listing Update Integration Tests ==="
start_server

# Wait for server to be ready
echo -n "0. Waiting for server... "
if wait_for_server; then
	pass "server ready"
else
	fail "server did not start"
fi

# 1. Verify empty listing shows "No poems yet"
echo -n "1. Empty listing check... "
out=$(curl -s "$BASE/poem/")
if echo "$out" | grep -q "No poems yet"; then
	pass "empty listing shows correct message"
else
	fail "expected 'No poems yet', got: $(echo "$out" | head -20)"
fi

# 2. Add first poem
echo -n "2. Add first poem... "
echo "This is the first test poem.
It has multiple lines." > "$TMPFILE1"
result=$(curl -sw "\n%{http_code}" -X POST "$BASE/poem/add" \
	-F "id=testpoem1" -F "file=@$TMPFILE1")
code=$(echo "$result" | tail -1)
body=$(echo "$result" | head -n -1)
if [ "$code" = "303" ]; then
	pass "first poem uploaded (303 redirect)"
elif [ "$code" = "400" ]; then
	fail "got 400 error. Response: $body"
else
	fail "expected 303, got $code. Response: $body"
fi

# 3. Verify first poem appears in listing
echo -n "3. First poem in listing... "
sleep 1  # Give filesystem a moment to sync
out=$(curl -s "$BASE/poem/")
if echo "$out" | grep -q 'href="/poem/testpoem1'; then
	pass "testpoem1 appears in listing"
else
	fail "testpoem1 not found in listing. HTML: $(echo "$out" | grep -E 'btn|poem' | head -10)"
fi

# 4. Verify "No poems yet" is gone
echo -n "4. No empty message... "
if ! echo "$out" | grep -q "No poems yet"; then
	pass "empty message removed"
else
	fail "'No poems yet' still present after adding poem"
fi

# 5. Add second poem
echo -n "5. Add second poem... "
echo "This is the second test poem.
Also with multiple lines." > "$TMPFILE2"
result=$(curl -sw "\n%{http_code}" -X POST "$BASE/poem/add" \
	-F "id=testpoem2" -F "file=@$TMPFILE2")
code=$(echo "$result" | tail -1)
body=$(echo "$result" | head -n -1)
if [ "$code" = "303" ]; then
	pass "second poem uploaded (303 redirect)"
elif [ "$code" = "400" ]; then
	fail "got 400 error. Response: $body"
else
	fail "expected 303, got $code. Response: $body"
fi

# 6. Verify both poems appear in listing
echo -n "6. Both poems in listing... "
sleep 1  # Give filesystem a moment to sync
out=$(curl -s "$BASE/poem/")
has_poem1=$(echo "$out" | grep -c 'href="/poem/testpoem1' || echo "0")
has_poem2=$(echo "$out" | grep -c 'href="/poem/testpoem2' || echo "0")
if [ "$has_poem1" -ge 1 ] && [ "$has_poem2" -ge 1 ]; then
	pass "both testpoem1 and testpoem2 in listing"
else
	fail "missing poems in listing (poem1: $has_poem1, poem2: $has_poem2)"
fi

# 7. Verify poem count
echo -n "7. Poem count correct... "
poem_links=$(echo "$out" | grep -o 'href="/poem/testpoem[12]' | wc -l)
if [ "$poem_links" -eq 2 ]; then
	pass "exactly 2 poems in listing"
else
	fail "expected 2 poems, found $poem_links"
fi

cleanup
echo ""
echo "All poem listing integration tests passed!"
