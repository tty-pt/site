#!/bin/sh
set -eu

# Simple smoke tests that assert a few pages render via the running axil server.
# Usage: AXIL_HOST=127.0.0.1 AXIL_PORT=8080 sh tests/pages/10-pages-render.sh

. "$(dirname "$0")/00-helpers.sh"

check(){
  route=$1
  expect_re=$2
  desc=$3

  out=$(curl_status_body "$route")
  code=$(printf '%s\n' "$out" | sed -n '1p')
  body=$(printf '%s\n' "$out" | sed -n '2,$p') || true

  if [ "$code" != "200" ]; then
    fail "$route returned HTTP $code"
  fi

  if ! printf '%s' "$body" | grep -q -E "$expect_re"; then
    fail "$route did not contain expected marker ($desc)"
  fi

  pass "$route: $desc"
}

check_cache_headers(){
  route=$1
  desc=$2

  headers=$(curl -sS -D - -o /dev/null "$BASE$route" | tr -d '\r') ||
    fail "$route header fetch failed"

  printf '%s' "$headers" | grep -q '^Cache-Control: no-cache' ||
    fail "$route missing Cache-Control: no-cache ($desc)"
  printf '%s' "$headers" | grep -q '^ETag:' ||
    fail "$route missing ETag ($desc)"
  printf '%s' "$headers" | grep -q '^Last-Modified:' ||
    fail "$route missing Last-Modified ($desc)"

  pass "$route: $desc"
}

echo "Running page smoke tests against $BASE"

check "/" "<!DOCTYPE html>" "root DOCTYPE"
check "/poem" "<title>[^<]*[Pp]oem|href=\"/poem" "poem page"
check "/song" "<title>[^<]*[Ss]ong|href=\"/song" "song page"
check "/gig" "<title>[^<]*[Ss]ongbook|href=\"/gig" "gig page"
check "/grp" "<title>[^<]*[Cc]hoir|href=\"/grp" "grp page"
check "/auth/login" "name=\"username\"|<form[^>]*action=\"/auth/login\"" "login form"
check "/auth/register" "name=\"email\"|<form[^>]*action=\"/auth/register\"" "register form"

check_cache_headers "/styles.css" "css cache validators"
check_cache_headers "/hyle.css" "css cache validators"

pass "pages smoke tests all OK"
