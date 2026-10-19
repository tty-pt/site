#!/bin/sh
set -eu

. "$(dirname "$0")/00-helpers.sh"

check_rejected(){
  route=$1
  tmp=$(mktemp)
  code=$(curl --path-as-is -sS --max-time 2 -o "$tmp" -w '%{http_code}' \
    "$BASE$route" 2>/dev/null || true)
  body=$(cat "$tmp" || true)
  rm -f "$tmp"

  case "$code" in
    2*|3*) fail "$route was not rejected (HTTP $code)" ;;
  esac
  printf '%s' "$body" | grep -q 'htdocs /' &&
    fail "$route exposed serve.allow"
  pass "$route: traversal rejected"
}

code=$(curl --path-as-is -sS --max-time 2 -o /dev/null -w '%{http_code}' \
  "$BASE/styles.css")
[ "$code" = 200 ] || fail "regular static asset returned HTTP $code"

check_rejected '/%2e%2e/serve.allow'
check_rejected '/%2E%2E%2fserve.allow'
check_rejected '/%252e%252e/serve.allow'
check_rejected '/safe%2f..%2f..%2fserve.allow'
check_rejected '/%2e%2e%5cserve.allow'
check_rejected '/%2e%2/serve.allow'
check_rejected '/api/dataset/song.items/%2e%2e/%2e%2e/serve.allow'

pass "encoded traversal tests all OK"
