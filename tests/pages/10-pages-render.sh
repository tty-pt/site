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

# Error page spacing check (HTML error response has space between code and message)
err_out=$(curl_status_body "/nonexistent_route_for_error_test")
err_code=$(printf '%s\n' "$err_out" | sed -n '1p')
err_body=$(printf '%s\n' "$err_out" | sed -n '2,$p') || true
if [ "$err_code" != "404" ]; then
  fail "expected 404 for nonexistent route, got $err_code"
fi
if ! printf '%s' "$err_body" | grep -q -E "<strong>404</strong>[[:space:]]+Not found|<!--bud-text:[0-9]+-->[[:space:]]+Not found"; then
  fail "404 error page missing whitespace between status code and message"
fi
pass "/nonexistent: 404 spacing test OK"

# Unauthenticated /song/add check (should render login form on HTML 401)
add_html_out=$(curl_status_body_html "/song/add")
add_html_code=$(printf '%s\n' "$add_html_out" | sed -n '1p')
add_html_body=$(printf '%s\n' "$add_html_out" | sed -n '2,$p') || true
if [ "$add_html_code" != "401" ]; then
  fail "expected 401 for unauthenticated HTML /song/add, got $add_html_code"
fi
if ! printf '%s' "$add_html_body" | grep -q "action=\"/auth/login\""; then
  fail "/song/add did not render login form on HTML 401"
fi
pass "/song/add: unauthorized renders login screen on HTML OK"

# HEAD request / curl -I checks
head_root=$(curl -sS -I --max-time 3 "$BASE/" | tr -d '\r')
printf '%s' "$head_root" | grep -q '^HTTP/1.1 200 OK' || fail "HEAD / did not return 200 OK"
printf '%s' "$head_root" | grep -q '^Content-Type: text/html' || fail "HEAD / missing Content-Type"
pass "HEAD /: 200 OK with headers"

head_css=$(curl -sS -I --max-time 3 "$BASE/styles.css" | tr -d '\r')
printf '%s' "$head_css" | grep -q '^HTTP/1.1 200 OK' || fail "HEAD /styles.css did not return 200 OK"
printf '%s' "$head_css" | grep -q '^Content-Length:' || fail "HEAD /styles.css missing Content-Length"
pass "HEAD /styles.css: 200 OK with Content-Length"

head_404=$(curl -sS -I --max-time 3 "$BASE/nonexistent" | tr -d '\r')
printf '%s' "$head_404" | grep -q '^HTTP/1.1 404 Not Found' || fail "HEAD /nonexistent did not return 404"
pass "HEAD /nonexistent: 404 Not Found"

pass "pages smoke tests all OK"
