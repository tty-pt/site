#!/bin/sh
set -eu

# Simple smoke tests that assert a few pages render via the running ndc server.
# Usage: NDC_HOST=127.0.0.1 NDC_PORT=8080 sh tests/pages/10-pages-render.sh

. "$(dirname "$0")/00-helpers.sh"

if ! wait_for_port "$NDC_HOST" "$NDC_PORT" 15; then
  fail "service not listening on $NDC_HOST:$NDC_PORT"
fi

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

echo "Running page smoke tests against $BASE"

check "/" "<!DOCTYPE html>" "root DOCTYPE"
check "/poem" "<title>[^<]*[Pp]oem|href=\"/poem" "poem page"
check "/login" "name=\"username\"|<form[^>]*action=\"/login\"" "login form"
check "/register" "name=\"email\"|<form[^>]*action=\"/register\"" "register form"
check "/api/index" "^\[" "api index returns JSON"

pass "pages smoke tests all OK"
