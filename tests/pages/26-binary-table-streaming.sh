#!/bin/sh
set -eu

. "$(dirname "$0")/00-helpers.sh"

echo "Running HYLT binary table streaming tests against $BASE"

# 1. Test GET /song?format=bin returns application/octet-stream with HYLT magic
tmp_bin=$(mktemp)
trap 'rm -f "$tmp_bin"' EXIT

hdr=$(curl -s -D - -o "$tmp_bin" "$BASE/song?format=bin")
printf '%s' "$hdr" | grep -qi "HTTP/1.1 200" || fail "/song?format=bin did not return 200 OK"
printf '%s' "$hdr" | grep -qi "content-type: application/octet-stream" || fail "/song?format=bin content-type is not application/octet-stream"

# Verify 4-byte magic is HYLT (0x48 0x59 0x4c 0x54)
magic=$(od -N 4 -c "$tmp_bin" | head -n 1 | awk '{print $2 $3 $4 $5}')
[ "$magic" = "HYLT" ] || fail "/song?format=bin magic is '$magic', expected 'HYLT'"
pass "GET /song?format=bin streams HYLT binary wire format"

# 2. Test GET /song with Accept: application/octet-stream header
hdr_acc=$(curl -s -D - -o "$tmp_bin" -H "Accept: application/octet-stream" "$BASE/song")
printf '%s' "$hdr_acc" | grep -qi "HTTP/1.1 200" || fail "Accept: application/octet-stream did not return 200 OK"
printf '%s' "$hdr_acc" | grep -qi "content-type: application/octet-stream" || fail "Accept: application/octet-stream content-type is not application/octet-stream"
magic_acc=$(od -N 4 -c "$tmp_bin" | head -n 1 | awk '{print $2 $3 $4 $5}')
[ "$magic_acc" = "HYLT" ] || fail "Accept: application/octet-stream magic is '$magic_acc', expected 'HYLT'"
pass "GET /song with Accept: application/octet-stream streams HYLT binary wire format"

# 3. Test GET /poem?format=bin
hdr_poem=$(curl -s -D - -o "$tmp_bin" "$BASE/poem?format=bin")
printf '%s' "$hdr_poem" | grep -qi "HTTP/1.1 200" || fail "/poem?format=bin did not return 200 OK"
printf '%s' "$hdr_poem" | grep -qi "content-type: application/octet-stream" || fail "/poem?format=bin content-type is not application/octet-stream"
magic_poem=$(od -N 4 -c "$tmp_bin" | head -n 1 | awk '{print $2 $3 $4 $5}')
[ "$magic_poem" = "HYLT" ] || fail "/poem?format=bin magic is '$magic_poem', expected 'HYLT'"
pass "GET /poem?format=bin streams HYLT binary wire format"

# 4. Standard request without format=bin returns HTML
hdr_html=$(curl -s -D - -o /dev/null "$BASE/song")
printf '%s' "$hdr_html" | grep -qi "content-type: text/html" || fail "default /song is not text/html"
pass "GET /song default returns text/html SSR"

# 5. format=json returns application/json
hdr_json=$(curl -s -D - -o /dev/null "$BASE/song?format=json")
printf '%s' "$hdr_json" | grep -qi "content-type: application/json" || fail "/song?format=json is not application/json"
pass "GET /song?format=json returns application/json"

# 6. Verify HTML contains binary preload link and meta bud-state-src
body_song=$(curl -fsS "$BASE/song/")
printf '%s' "$body_song" | grep -q '<link rel="preload" href="/song/?format=bin" as="fetch" type="application/octet-stream" crossorigin>' || fail "missing binary preload link in HTML head"
printf '%s' "$body_song" | grep -q '<meta name="bud-state-src" content="/song/?format=bin">' || fail "missing meta[name=bud-state-src] in HTML head"
pass "HTML head contains <link rel=preload> and <meta name=bud-state-src>"

# 7. Verify filtered query string preserves parameters in preload link and meta tag
body_filtered=$(curl -fsS "$BASE/song/?genre=rock")
printf '%s' "$body_filtered" | grep -q '<link rel="preload" href="/song/?genre=rock&format=bin"' || fail "missing filtered binary preload link in HTML head"
printf '%s' "$body_filtered" | grep -q '<meta name="bud-state-src" content="/song/?genre=rock&format=bin">' || fail "missing filtered meta[name=bud-state-src] in HTML head"
pass "HTML head preserves query parameters in binary preload URL"

echo "ALL HYLT BINARY TABLE STREAMING TESTS PASSED"
