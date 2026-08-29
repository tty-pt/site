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
  post_args=""
  for kv in "$@"; do
    post_args="$post_args -F $kv"
  done
  code=$(curl -sS --max-time 5 -b "$JAR" -c "$JAR" -o "$BODY" \
    -w '%{http_code}' $post_args "$BASE$path" || true)
}

api_csrf(){
  get /api/csrf
  CSRF=$(cat "$BODY")
}

# ── auth (AUTH_SKIP_CONFIRM=1 dev server required) ────────────────────
U="pick$(date +%s)"
api_csrf
code=$(curl -sS --max-time 5 -b "$JAR" -c "$JAR" -o "$BODY" -w '%{http_code}' -X POST "$BASE/auth/register" \
  -d "username=$U&password=picktest-pass-1&password2=picktest-pass-1&csrf_token=$CSRF")
case "$code" in 2*|3*) ;; *) fail "register failed (HTTP $code)" ;; esac
api_csrf
code=$(curl -sS --max-time 5 -b "$JAR" -c "$JAR" -o "$BODY" -w '%{http_code}' -X POST "$BASE/auth/login" \
  -d "username=$U&password=picktest-pass-1&csrf_token=$CSRF")
case "$code" in 2*|3*) ;; *) fail "login failed (HTTP $code)" ;; esac
pass "authenticated session established"

# ── anon rejections ───────────────────────────────────────────────────
anon_code=$(curl -sS --max-time 5 -o /dev/null -w '%{http_code}' \
  "$BASE/pick/song.types/options?key=type&q=&page=1&sel=")
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
grep -q 'data-hyle-picker-key="author"' "$BODY" ||
  fail "song/add picker missing key=author"
grep -q 'data-hyle-frag-url=' "$BODY" ||
  fail "song/add picker missing frag-url template"
grep -q 'id="pickq-type"' "$BODY" ||
  fail "song/add missing sibling GET form for type"
grep -q 'id="pickq-author"' "$BODY" ||
  fail "song/add missing sibling GET form for author"
grep -q 'name="pick_q_type"' "$BODY" ||
  fail "song/add missing picker search box for type"
grep -q 'name="pick_q_author"' "$BODY" ||
  fail "song/add missing picker search box for author"
grep -q 'hyle-fragments.js' "$BODY" ||
  fail "song/add missing fragments transport script"
pass "song/add picker markup complete"

# ── fragment envelope shape (logged in) ───────────────────────────────
get "/pick/song.types/options?key=type&q=&page=1&sel=&more=1"
[ "$code" = 200 ] || fail "fragment route returned $code"
grep -q '"rows"' "$BODY" || fail "envelope missing rows"
grep -q '"eof"' "$BODY" || fail "envelope missing eof"
grep -q 'hyle-picker-rows' "$BODY" && fail "append fragment must not wrap in hyle-picker-rows container"
pass "type fragment envelope shape OK"

get "/pick/song.authors/options?key=author&q=&page=1&sel=&more=1"
[ "$code" = 200 ] || fail "author fragment route returned $code"
grep -q '"rows"' "$BODY" || fail "envelope missing rows"
grep -q '"eof"' "$BODY" || fail "envelope missing eof"
grep -q 'hyle-picker-rows' "$BODY" && fail "append fragment must not wrap in hyle-picker-rows container"
pass "author fragment envelope shape OK"

# ── empty append must return empty rows and eof=1 ─────────────────────
get "/pick/song.types/options?key=type&pick_q_type=NonExistentQuery999&page=1&sel=&more=1"
[ "$code" = 200 ] || fail "empty append returned $code"
grep -q '"rows":""' "$BODY" || fail "empty append must have empty rows string, got: $(cat "$BODY")"
grep -q '"eof":1' "$BODY" || fail "empty append must have eof:1"
pass "empty append envelope shape OK"

# ── fragment search with query renders inline add record button ───────
get "/pick/song.types/options?key=type&add=1&pick_q_type=BrandNewType&page=1&sel="
[ "$code" = 200 ] || fail "type fragment with query returned $code"
grep -q 'data-hyle-picker-add' "$BODY" || fail "type panel missing data-hyle-picker-add"
grep -q 'data-hyle-picker-source=\\"song.types\\"' "$BODY" || fail "type panel missing source song.types"
grep -q '&amp;ldquo;' "$BODY" && fail "type panel has double-escaped &amp;ldquo; entity"
grep -q '&ldquo;' "$BODY" && fail "type panel has raw &ldquo; entity in HTML"
grep -q '“BrandNewType”' "$BODY" || fail "type panel missing UTF-8 quotes for BrandNewType"
pass "type fragment with query renders add button"

# ── fragment search with add=0 does NOT render inline add record button ─
get "/pick/song.types/options?key=fmt&add=0&pick_q_fmt=BrandNewType&page=1&sel="
[ "$code" = 200 ] || fail "format fragment with add=0 returned $code"
grep -q 'data-hyle-picker-add' "$BODY" && fail "format panel with add=0 must NOT have data-hyle-picker-add"
pass "format fragment with add=0 suppresses add button"

get "/pick/song.authors/options?key=author&add=1&pick_q_author=BrandNewAuthor&page=1&sel="
[ "$code" = 200 ] || fail "author fragment with query returned $code"
grep -q 'data-hyle-picker-add' "$BODY" || fail "author panel missing data-hyle-picker-add"
grep -q 'data-hyle-picker-source=\\"song.authors\\"' "$BODY" || fail "author panel missing source song.authors"
grep -q '&amp;ldquo;' "$BODY" && fail "author panel has double-escaped &amp;ldquo; entity"
grep -q '&ldquo;' "$BODY" && fail "author panel has raw &ldquo; entity in HTML"
grep -q '“BrandNewAuthor”' "$BODY" || fail "author panel missing UTF-8 quotes for BrandNewAuthor"
pass "author fragment with query renders add button"

# ── create record via dataset endpoint for authors and types ──────────
api_csrf
code=$(curl -sS --max-time 5 -b "$JAR" -c "$JAR" -o "$BODY" -w '%{http_code}' -X POST \
  -d "name=Test Author $$&csrf_token=$CSRF" "$BASE/api/dataset/song.authors")
case "$code" in 200|201) ;; *) fail "POST /api/dataset/song.authors returned $code" ;; esac
NEW_AUTH_ID=$(grep -o '"id": *"[^"]*"' "$BODY" | head -1 | sed 's/.*: *"//;s/"//')
[ -n "$NEW_AUTH_ID" ] || fail "missing id in new author response"
pass "author dataset item creation OK ($NEW_AUTH_ID)"

api_csrf
code=$(curl -sS --max-time 5 -b "$JAR" -c "$JAR" -o "$BODY" -w '%{http_code}' -X POST \
  -d "name=Test Type $$&csrf_token=$CSRF" "$BASE/api/dataset/song.types")
case "$code" in 200|201) ;; *) fail "POST /api/dataset/song.types returned $code" ;; esac
NEW_TYPE_ID=$(grep -o '"id": *"[^"]*"' "$BODY" | head -1 | sed 's/.*: *"//;s/"//')
[ -n "$NEW_TYPE_ID" ] || fail "missing id in new type response"
pass "type dataset item creation OK ($NEW_TYPE_ID)"

# ── repeated-key wire format: multi-ref accepts repeated parts ────────
T1=$(curl -sS --max-time 5 -b "$JAR" \
  "$BASE/api/dataset/song.types?per_page=1" |
  grep -o '"id": *"[^"]*"' | head -1 | sed 's/.*: *"//;s/"//')
[ -n "$T1" ] || fail "no song.types fixtures found"
T2=$(curl -sS --max-time 5 -b "$JAR" \
  "$BASE/api/dataset/song.types?per_page=5" |
  grep -o '"id": *"[^"]*"' | sed -n 2p | sed 's/.*: *"//;s/"//')
[ -n "$T2" ] || T2="$T1"
SONG_TITLE="PickPages Song $(date +%s)"
api_csrf
LOC=$(curl -sS --max-time 5 -b "$JAR" -c "$JAR" -o /dev/null \
  -w '%{redirect_url}' -X POST \
  -F "title=$SONG_TITLE" -F "author=$NEW_AUTH_ID" \
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

# ── author selection present on edit ──────────────────────────────────
get "/song/$SONG_ID/edit"
grep -q "value=\"$NEW_AUTH_ID\"" "$BODY" || fail "edit page missing new author value"
grep -q -E "value=\"$NEW_AUTH_ID\"[^>]*checked" "$BODY" || grep -q -E "checked[^>]*value=\"$NEW_AUTH_ID\"" "$BODY" || fail "edit page author not checked"
pass "author selection present and checked on edit"

# ── pinned selections present under any q/page on edit ────────────────
for QS in "" "?pick_q_type=zzzznomatch&page=1" "?pick_q_type=&page=3"; do
  get "/song/$SONG_ID/edit$QS"
  grep -q -E 'checked(="")?' "$BODY" ||
    fail "edit$QS lost pinned selection"
done
pass "pinned selections survive q/page variations"

# sibling mirror carries values
get "/song/$SONG_ID/edit"
grep -q 'id="pickq-type"' "$BODY" || fail "edit missing sibling form"
grep -q 'name="type" value=' "$BODY" ||
  fail "edit missing type mirrors"
pass "GET-form mirrors present on edit"

# ── gig single-ref draft preselect (?grp= kept visible) ───────────────
GRP_ID=$(curl -sS --max-time 5 -b "$JAR" "$BASE/api/dataset/grp.items?per_page=1" |
  grep -o '"id": *"[^"]*"' | head -1 | sed 's/.*: *"//;s/"//')
if [ -n "$GRP_ID" ]; then
  get "/gig/add?grp=$GRP_ID"
  if grep -q 'select[^>]*name="grp"' "$BODY"; then
    grep -q "value=\"$GRP_ID\" selected" "$BODY" ||
      fail "draft grp not preselected in inline select"
  else
    grep -q "value=\"$GRP_ID\"" "$BODY" ||
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
grep -qE '<(input|textarea)[^>]*name="type"' "$BODY" ||
  fail "fallback field missing after cutoff"
pass "QS budget cutoff falls back to text input"

pass "50-pickers: all OK"
