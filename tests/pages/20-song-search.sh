#!/bin/sh
set -eu

# Full-text (stoma) search semantics on the song listing.
# Pins the token-prefix behavior that replaced ci_substr substring matching:
#  - "star" is NOT a token prefix of "estar"/"Starlight" -> 0 rows (old code matched)
#  - "cor" IS a token prefix ("Coração", "Corações") -> rows
#  - accent-sensitive: "coracao" does NOT match "Coração" -> 0 rows
#  - multi-field AND across two searchable string fields
# Usage: AXIL_HOST=127.0.0.1 AXIL_PORT=8080 sh tests/pages/20-song-search.sh

. "$(dirname "$0")/00-helpers.sh"

row_count(){
  route=$1
  out=$(curl_status_body "$route")
  code=$(printf '%s\n' "$out" | sed -n '1p')
  body=$(printf '%s\n' "$out" | sed -n '2,$p') || true

  if [ "$code" != "200" ]; then
    fail "$route returned HTTP $code"
  fi

  printf '%s' "$body" | grep -oE '[0-9]+ of [0-9]+ rows' | head -1
}

expect_zero(){
  route=$1
  desc=$2
  count=$(row_count "$route")
  if [ "$count" != "0 of 0 rows" ]; then
    fail "$route: expected '0 of 0 rows', got '$count' ($desc)"
  fi
  pass "$route: $desc"
}

expect_rows(){
  route=$1
  desc=$2
  count=$(row_count "$route")
  if [ -z "$count" ] || [ "$count" = "0 of 0 rows" ]; then
    fail "$route: expected rows, got '$count' ($desc)"
  fi
  pass "$route: $desc"
}

echo "Running FTS song-search smoke tests against $BASE"

expect_zero "/song/?title=star" "mid-word 'star' must NOT match 'estar' (prefix semantics)"
expect_rows "/song/?title=cor" "token prefix 'cor' matches"
expect_zero "/song/?title=coracao" "accent-sensitive: 'coracao' must NOT match 'Coração'"
expect_zero "/song/?title=zzzzzz" "non-matching value -> 0 rows"
expect_rows "/song/?title=cor&author=joaquim" "multi-field AND (title+author)"
expect_zero "/song/?title=cor&author=zzzz" "multi-field AND with bad author -> 0 rows"

pass "FTS song-search smoke tests all OK"
