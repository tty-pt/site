#!/bin/sh
# Memory Mipmaps test suite: unit tests plus end-to-end CLI exercise.
set -e
cd "$(dirname "$0")"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

F="$TMP/mem.qmap"

fail() {
	echo "FAIL: $1" >&2
	exit 1
}

pass() {
	echo "ok: $1"
}

./bin/engine_test

MM="./bin/mm"

$MM store --file "$F" --level 2 --topic mirror --ts 2025-05 \
	--text "May gist: mirror and rain." || fail "store L2 mirror May"
$MM store --file "$F" --level 2 --topic mirror --ts 2025-06 \
	--text "June gist: puddle." || fail "store L2 mirror June"
$MM store --file "$F" --level 2 --topic song --ts 2025-05 \
	--text "Song gist." || fail "store L2 song"
$MM store --file "$F" --level 1 --topic mirror --ts 2025-05-14T17:05 \
	--text "condensed insight A" || fail "store L1"
$MM store --file "$F" --level 0 --ts "2025-05-14T17:05" \
	--text "user raw transcript" || fail "store L0"
pass "store"

OUT=$($MM scan --file "$F" --topic mirror --level 2)
N=$(printf '%s' "$OUT" | grep -c '^mirror@') || true
[ "$N" = "2" ] || fail "scan mirror/L2 expected 2, got $N"
case "$OUT" in
	mirror@2025-06*) : ;;
	*) fail "scan order: newest first violated" ;;
esac
pass "scan topic routing + order"

OUT=$($MM scan --file "$F" --prefix "@2025-05-14T17:05")
N=$(printf '%s' "$OUT" | grep -c '^@2025') || true
[ "$N" = "1" ] || fail "prefix zoom expected 1, got $N"
pass "prefix zoom to raw"

OUT=$($MM scan --file "$F" --q "mirror rain")
N=$(printf '%s' "$OUT" | grep -c '^mirror@') || true
[ "$N" = "1" ] || fail "fts AND expected 1, got $N"
pass "fts AND"

OUT=$($MM scan --file "$F" --q '"condensed insight A"')
N=$(printf '%s' "$OUT" | grep -c '^mirror@') || true
[ "$N" = "1" ] || fail "fts phrase expected 1, got $N"
pass "fts phrase"

$MM store --file "$F" --level 1 --topic food --ts "2025-05-14T17:20" \
	--text "A refeição tinha pão fresco." || fail "store accent"
OUT=$($MM scan --file "$F" --q "pao")
N=$(printf '%s' "$OUT" | grep -c '^food@') || true
[ "$N" = "0" ] || fail "accent: pao matched pão"
OUT=$($MM scan --file "$F" --q "pão")
N=$(printf '%s' "$OUT" | grep -c '^food@') || true
[ "$N" = "1" ] || fail "accent: pão did not match"
pass "accent sensitivity"

$MM vec put --file "$F" --key "v1" --text "0 1 0" || fail "vec put"
OUT=$($MM vec cos --file "$F" --key v1 --key v1)
[ "$OUT" = "1.000000" ] || fail "cos self expected 1.0, got $OUT"
pass "vectors + cosine"

# ---- semantic scan (offline primitives: --vec and --like) ------------
$MM vec put --file "$F" --key "mirror@2025-05" --text "1 0 0" || fail "vec mirror@2025-05"
$MM vec put --file "$F" --key "mirror@2025-06" --text "0 1 0" || fail "vec mirror@2025-06"
$MM vec put --file "$F" --key "mirror@2025-05-14T1705" --text "0.9 0.1 0" || fail "vec L1"

OUT=$($MM scan --file "$F" --vec "0 1 0.1")
N=$(printf '%s' "$OUT" | grep -c '^mirror') || true
[ "$N" = "3" ] || fail "semantic --vec expected 3, got $N"
case "$OUT" in
	mirror@2025-06*) : ;;
	*) fail "semantic --vec: top cosine first" ;;
esac
echo "$OUT" | grep -q 'mirror@2025-06	2	2025-06' || fail "semantic split field lost"
pass "semantic --vec"

OUT=$($MM scan --file "$F" --like "mirror@2025-06")
case "$OUT" in
	mirror@2025-06*) : ;;
	*) fail "semantic --like: self first" ;;
esac
pass "semantic --like"

OUT=$($MM scan --file "$F" --vec "0 0 1")
case "$OUT" in
	"") fail "semantic: orthogonal entries should still return at min_sim 0" ;;
	*) : ;;
esac
OUT=$($MM scan --file "$F" --vec "0 0 1" --min-sim 0.5)
[ -z "$OUT" ] || fail "semantic --min-sim 0.5 should drop orthogonal, got: $OUT"
pass "semantic --min-sim"

OUT=$($MM scan --file "$F" --vec "1 0 0" --max 1)
N=$(printf '%s' "$OUT" | grep -c '^mirror') || true
[ "$N" = "1" ] || fail "semantic --max 1 expected 1, got $N"
pass "semantic --max"

# ---- --embed via a stub curl over a fake provider --------------------
STUB="$TMP/stubbin"
mkdir -p "$STUB"
cat > "$STUB/curl" <<'EOF'
#!/bin/sh
printf '%s' '{"data":[{"embedding":[1.0,0.0,0.0]}]}'
EOF
chmod +x "$STUB/curl"

$MM scan --file "$F" --embed "anything" >/dev/null 2>&1 && fail "embed without provider should fail"
OUT=$(MM_EMBED_URL="http://fake" MM_EMBED_KEY="k" PATH="$STUB:$PATH" \
	$MM scan --file "$F" --embed "text to embed" 2>&1)
case "$OUT" in
	mirror@2025-05*) : ;;
	*) fail "semantic --embed (stub) should rank mirror@2025-05 first, got: $OUT" ;;
esac
pass "semantic --embed (stub curl)"

OUT=$(MM_EMBED_URL="http://fake" MM_EMBED_KEY="k" PATH="$STUB:$PATH" \
	$MM store --file "$F" --level 2 --topic embed --ts 2025-07 \
	--text "embedded gist" --embed 2>&1) || fail "store --embed (stub)"
[ -z "$OUT" ] || fail "store --embed unexpected output: $OUT"
OUT=$($MM vec get --file "$F" --key "embed@2025-07")
[ "$OUT" = "1.000000 0.000000 0.000000" ] || fail "store --embed vector, got: $OUT"
pass "store --embed (stub curl)"

$MM forget --file "$F" --key "mirror@2025-05" || fail "forget"
OUT=$($MM scan --file "$F" --topic mirror --level 2)
N=$(printf '%s' "$OUT" | grep -c '^mirror@') || true
[ "$N" = "1" ] || fail "forget: expected 1 left, got $N"
pass "forget"

$MM reset --file "$F" || fail "reset"
OUT=$($MM scan --file "$F")
[ -z "$OUT" ] || fail "reset: store not empty"
pass "reset"

echo ""
echo "ALL CLI TESTS PASSED"