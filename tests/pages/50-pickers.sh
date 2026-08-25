#!/bin/sh
# Omnisearch picker form integration (OMNI-DROPDOWN phase 7).
# Covers: descriptor rendering, pinned selections under any q/page,
# sibling GET mirrors, repeated-key wire format, fragment envelope
# shape, budget-cutoff fallback, anon rejection.
set -eu

. "$(dirname "$0")/00-helpers.sh"

JAR=$(mktemp)
BODY=$(mktemp)
trap 'rm -f "$JAR" "$BODY"' EXIT

get(){ # get path [extra curl args...]
  path=$1; shift
  code=$(curl -sS --max-time 5 -b "$JAR" -c "$JAR" -o "$BODY" \
    -w '%{http_code}' "$BASE$path" "$@" || true)
}

post_multipart(){ # post_multipart path field=value...
  path=$1; shift
  set --
  for kv in "$@"; do
    set -- "$@" -F "$kv"
  done
  code=$(curl -sS --max-time 5 -b "$JAR" -c "$JAR" -o "$BODY" \
    -w '%{http_code}' "$@" "$BASE$path" || true)
}

api_csrf(){
  get /api/csrf
  CSRF=$(cat "$BODY")
}

# ── auth (AUTH_SKIP_CONFIRM=1 dev server required) ────────────────────
U="pick$(date +%s)"
api_csrf
post_multipart /auth/register \
  "username=$U" "password=picktest-pass-1" "password2=picktest-pass-1" \
  "csrf_token=$CSRF"
case "$code" in 2*|3*) ;; *) fail "register failed (HTTP $code)" ;; esac
api_csrf
post_multipart /auth/login "username=$U" "password=picktest-pass-1" \
  "csrf_token=$CSRF"
case "$code" in 2*|3*) ;; *) fail "login failed (HTTP $code)" ;; esac
pass "authenticated session established"

# ── anon rejections ───────────────────────────────────────────────────
anon_code=$(curl -sS --max-time 5 -o /dev/null -w '%{http_code}' \
  "$BASE/pick/song.types/options?q=&page=1&sel=")
[ "$anon_code" = 401 ] || fail "anon fragment route returned $anon_code, want 401"
pass "anon fragment route -> 401"

anon_code=$(curl -sS --max-time 5 -o /dev/null -w '%{http_code}' \
  "$BASE/song/add")
case "$anon_code" in
  3*|401) ;;
  *) fail "anon form page returned $anon_code, want redirect/401" ;;
esac
pass "anon form page rejected"

# ── song/add renders the omnisearch picker + sibling mirror form ──────
get /song/add
grep -q 'data-hyle-picker' "$BODY" ||
  fail "song/add missing data-hyle-picker container"
grep -q 'data-hyle-picker-key="type"' "$BODY" ||
  fail "song/add picker missing key=type"
grep -q 'data-hyle-frag-url=' "$BODY" ||
  fail "song/add picker missing frag-url template"
grep -q 'id="pickq-type"' "$BODY" ||
  fail "song/add missing sibling GET form"
grep -q 'name="pick_q_type"' "$BODY" ||
  fail "song/add missing picker search box"
grep -q 'hyle-fragments.js' "$BODY" ||
  fail "song/add missing fragments transport script"
pass "song/add picker markup complete"

# ── fragment envelope shape (logged in) ───────────────────────────────
get "/pick/song.types/options?q=&page=1&sel="
[ "$code" = 200 ] || fail "fragment route returned $code"
grep -q '"rows"' "$BODY" || fail "envelope missing rows"
grep -q '"eof"' "$BODY" || fail "envelope missing eof"
pass "fragment envelope shape OK"

# ── repeated-key wire format: multi-ref accepts repeated parts ────────
T1=$(curl -sS --max-time 5 \
  "$BASE/api/dataset/song.types?per_page=1" |
  grep -o '"id": *"[^"]*"' | head -1 | sed 's/.*: *"//;s/"//')
[ -n "$T1" ] || fail "no song.types fixtures found"
T2=$(curl -sS --max-time 5 \
  "$BASE/api/dataset/song.types?per_page=5" |
  grep -o '"id": *"[^"]*"' | sed -n 2p | sed 's/.*: *"//;s/"//')
[ -n "$T2" ] || T2="$T1"
SONG_TITLE="PickPages Song $(date +%s)"
api_csrf
LOC=$(curl -sS --max-time 5 -b "$JAR" -c "$JAR" -o /dev/null \
  -w '%{redirect_url}' -X POST \
  -F "title=$SONG_TITLE" -F "author=t" \
  -F "type=$T1" -F "type=$T2" \
  -F "csrf_token=$CSRF" "$BASE/song/add")
case "$LOC" in
  */song/*) SONG_ID=${LOC##*/} ;;
  *) fail "multi-type song create failed (loc=$LOC)" ;;
esac
[ -d "var/song/$SONG_ID" ] || fail "song dir missing for $SONG_ID"
pass "repeated-key create accepted ($T1,$T2)"

# stored file joined by newline (write contract)
if [ -f "var/song/$SONG_ID/type" ]; then
  LINES=$(wc -l < "var/song/$SONG_ID/type")
  [ "$LINES" -ge 1 ] || fail "type file empty"
fi

# ── pinned selections present under any q/page on edit ────────────────
for QS in "" "?pick_q_type=zzzznomatch&page=1" "?pick_q_type=&page=3"; do
  get "/song/$SONG_ID/edit$QS"
  grep -q 'checked=""' "$BODY" ||
    fail "edit$QS lost pinned selection"
done
pass "pinned selections survive q/page variations"

# sibling mirror carries values
get "/song/$SONG_ID/edit"
grep -q 'id="pickq-type"' "$BODY" || fail "edit missing sibling form"
printf '%s' "$BODY" | grep -q 'name="type" value=' ||
  fail "edit missing type mirrors"
pass "GET-form mirrors present on edit"

# ── gig single-ref draft preselect (?grp= kept visible) ───────────────
GRP_ID=$(curl -sS --max-time 5 "$BASE/api/dataset/grp.items?per_page=1" |
  grep -o '"id": *"[^"]*"' | head -1 | sed 's/.*: *"//;s/"//')
if [ -n "$GRP_ID" ]; then
  get "/gig/add?grp=$GRP_ID"
  if grep -q 'select[^>]*name="grp"' "$BODY"; then
    printf '%s' "$BODY" | grep -q "value=\"$GRP_ID\" selected" ||
      fail "draft grp not preselected in inline select"
  else
    printf '%s' "$BODY" | grep -q "value=\"$GRP_ID\"" ||
      fail "draft grp option missing from picker"
  fi
  pass "?grp= draft preselect rendered"
else
  pass "?grp= preselect skipped (no grp fixtures)"
fi

# ── budget cutoff: oversized query falls back to plain text field ─────
BIGQS=$(printf 'x%.0s' $(seq 1 2200))
get "/song/add?type=$BIGQS"
if grep -q 'data-hyle-picker-key="type"' "$BODY"; then
  fail "budget cutoff did not engage (picker still bound to type)"
fi
printf '%s' "$BODY" | grep -qE '<(input|textarea)[^>]*name="type"' ||
  fail "fallback field missing after cutoff"
pass "QS budget cutoff falls back to text input"

pass "50-pickers: all OK"
