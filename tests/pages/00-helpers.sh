#!/bin/sh
set -eu

: ${NDC_HOST:=127.0.0.1}
: ${NDC_PORT:=8080}

BASE="http://$NDC_HOST:$NDC_PORT"

fail(){ echo "FAIL: $*"; exit 1; }
pass(){ echo "PASS: $*"; }

wait_for_port(){
  host=$1; port=$2; timeout=${3:-15}
  i=0
  while ! nc -z "$host" "$port" 2>/dev/null; do
    i=$((i+1))
    if [ "$i" -ge "$timeout" ]; then
      return 1
    fi
    sleep 1
  done
  return 0
}

curl_status_body(){
  url=$1
  tmp=$(mktemp)
  code=$(curl -sS -L -w "%{http_code}" -o "$tmp" "$BASE$url" || true)
  body=$(cat "$tmp" || true)
  rm -f "$tmp"
  echo "$code"
  printf "%s" "$body"
}
