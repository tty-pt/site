#!/bin/sh
set -eu

. "$(dirname "$0")/00-helpers.sh"

check_view(){
  route=$1
  display=$2
  fields_re=$3
  desc=$4

  body=$(curl -fsS "$BASE$route") || fail "$route fetch failed"
  printf '%s' "$body" | grep -q "\"display_name\":\"$display\"" ||
    fail "$route missing source-owned display name ($desc)"
  printf '%s' "$body" | grep -q -E "$fields_re" ||
    fail "$route has wrong list field order/labels ($desc)"
  pass "$route: $desc"
}

echo "Running source-owned list metadata tests against $BASE"

check_view "/song?custom=1" "song" \
  '"key":"title","label":"Title".*"key":"type","label":"Type".*"key":"author","label":"Author"' \
  "song metadata and order"
check_view "/poem?custom=1" "poem" \
  '"key":"title","label":"Title".*"key":"owner","label":"Owner"' \
  "poem metadata and order"
check_view "/gig?custom=1" "gig" \
  '"key":"title","label":"Title".*"key":"grp","label":"Group"' \
  "gig metadata and group label"
check_view "/grp?custom=1" "group" \
  '"ncols":1,"cols":\[\{"key":"title","label":"Title"' \
  "group display metadata"

song=$(curl -fsS "$BASE/song?custom=1") || fail "/song fetch failed"
printf '%s' "$song" | grep -q '"content_field":"data"' ||
  fail "/song missing source-owned content field"
printf '%s' "$song" | grep -q 'name="data"' ||
  fail "/song did not render source-owned content field"

if grep -E 'idx_select_fields_for|idx_display_name|strcmp\([^)]*"(song|poem|gig|grp)"' \
  mods/index/index.c mods/index/ux/list.c >/dev/null; then
  fail "mods/index still contains module-specific list switches"
fi

pass "list metadata is source-owned"
