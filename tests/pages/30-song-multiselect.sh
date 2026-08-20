#!/bin/sh
set -eu

# Multi-ref dropdown filter (song.type) SSR smoke tests.
# Verifies:
#  - the SSR contract: <details class="hyle-multiselect" data-hyle-ms="type">
#    with one real checkbox per option (no-JS baseline)
#  - repeated-key union filtering: ?type=natal&type=comunhao returns songs of
#    either type and excludes a song of neither
#  - the WASM enhancement hooks: id="bud-state" + data-modules="list"
#  - filter round-trip: ?type=natal marks the natal checkbox checked and shows
#    "Natal" in the trigger label
# Usage: AXIL_HOST=127.0.0.1 AXIL_PORT=8080 sh tests/pages/30-song-multiselect.sh

. "$(dirname "$0")/00-helpers.sh"

body_of(){
  route=$1
  out=$(curl_status_body "$route")
  code=$(printf '%s\n' "$out" | sed -n '1p')
  body=$(printf '%s\n' "$out" | sed -n '2,$p') || true
  if [ "$code" != "200" ]; then
    fail "$route returned HTTP $code"
  fi
  printf '%s' "$body"
}

row_count(){
  result=$(printf '%s' "$1" | grep -oE '[0-9]+ of [0-9]+ rows' | head -1)
  if [ -z "$result" ] && printf '%s' "$1" | grep -q 'No items'; then
    result="0 of 0 rows"
  fi
  printf '%s' "$result"
}

echo "Running song multiselect SSR smoke tests against $BASE"

song=$(body_of "/song/?custom=1")

printf '%s' "$song" | grep -q 'data-hyle-ms="type"' \
  || fail "/song/?custom=1 missing data-hyle-ms=\"type\""
pass "/song/?custom=1 has data-hyle-ms=\"type\""

printf '%s' "$song" | grep -q '<details[^>]*class="hyle-multiselect"' \
  || fail "/song/?custom=1 missing <details class=\"hyle-multiselect\">"
pass "/song/?custom=1 has the <details> SSR contract"

printf '%s' "$song" | grep -q '<input[^>]*type="checkbox"[^>]*name="type"' \
  || fail "/song/?custom=1 missing real type checkboxes (no-JS baseline)"
pass "/song/?custom=1 has real <input type=checkbox name=type> controls"

printf '%s' "$song" | grep -q 'id="bud-state"' \
  || fail "/song/?custom=1 missing bud-state JSON"
pass "/song/?custom=1 embeds id=\"bud-state\""

printf '%s' "$song" | grep -q 'data-modules="list"' \
  || fail "/song/?custom=1 missing data-modules=\"list\""
pass "/song/?custom=1 opts into data-modules=\"list\""

filtered=$(body_of "/song/?custom=1&type=natal&type=comunhao&type_op=or")
count=$(row_count "$filtered")
case "$count" in
  *" of 0 rows"|"")
    fail "/song/?custom=1&type=natal&type=comunhao&type_op=or: expected rows (union), got '$count'";;
esac
pass "/song/?custom=1&type=natal&type=comunhao&type_op=or filters (union): $count"

printf '%s' "$filtered" | grep -q 'a_ele_a_gloria' \
  && fail "union filter must exclude a Louvor-only song (a_ele_a_gloria)"
pass "union filter excludes a song of neither type"

and=$(body_of "/song/?custom=1&type=natal&type=saida")
printf '%s' "$and" | grep -q 'alegremse_os_ceus_e_a_terra' \
  || fail "AND filter must include the dual-typed song (alegremse_os_ceus_e_a_terra)"
pass "AND filter returns a song with both natal and saida"
printf '%s' "$and" | grep -q 'a_ele_a_gloria' \
  && fail "AND filter must exclude Louvor-only a_ele_a_gloria"
pass "AND filter excludes a song of neither selected type"
and_count=$(row_count "$and")
case "$and_count" in
  "0 of 0 rows"|"")
    fail "/song/?custom=1&type=natal&type=saida (AND) returned no rows — expected matches";;
esac
pass "AND filter has rows: $and_count"

and_empty=$(body_of "/song/?custom=1&type=natal&type=comunhao")
and_empty_count=$(row_count "$and_empty")
case "$and_empty_count" in
  "0 of 0 rows")
    pass "AND ?type=natal&type=comunhao correctly returns 0 rows (no dual song)";;
  *)
    fail "AND ?custom=1&type=natal&type=comunhao should return 0 rows, got '$and_empty_count'";;
esac

single=$(body_of "/song/?custom=1&type=natal")
printf '%s' "$single" | grep -q 'value="natal" checked' \
  || fail "/song/?custom=1&type=natal: natal checkbox not checked on round-trip"
pass "/song/?custom=1&type=natal round-trips checked state"

printf '%s' "$single" | grep -q 'Natal' \
  || fail "/song/?custom=1&type=natal: trigger label missing 'Natal'"
pass "/song/?custom=1&type=natal trigger label shows the selection"

sb=$(body_of "/gig/?custom=1")
printf '%s' "$sb" | grep -q 'data-hyle-ss="grp"' \
  || fail "/gig/?custom=1 missing data-hyle-ss=\"grp\""
pass "/gig/?custom=1 has the dropdown single-select"
printf '%s' "$sb" | grep -q 'name="grp"' \
  || fail "/gig/?custom=1 missing radio name=\"grp\" (no-JS baseline)"
pass "/gig/?custom=1 has real <input type=radio name=grp> controls"

pass "song multiselect SSR smoke tests all OK"
