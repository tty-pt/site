#!/bin/sh
set -eu

. "$(dirname "$0")/00-helpers.sh"

echo "Running multi-column table sorting integration tests against $BASE"

tmp_bin=$(mktemp)
trap 'rm -f "$tmp_bin"' EXIT

test_sort() {
  module=$1
  col=$2
  dir=$3
  url="/${module}/?sort=${col}:${dir}"

  # 1. Test binary format streaming
  bin_url="${url}&format=bin"
  hdr_bin=$(curl -s -D - -o "$tmp_bin" "$BASE$bin_url")
  printf '%s' "$hdr_bin" | grep -qi "HTTP/1.1 200" || fail "Binary $bin_url did not return 200 OK"
  printf '%s' "$hdr_bin" | grep -qi "content-type: application/octet-stream" || fail "Binary $bin_url is not application/octet-stream"

  magic=$(od -N 4 -c "$tmp_bin" | head -n 1 | awk '{print $2 $3 $4 $5}')
  [ "$magic" = "HYLT" ] || fail "Binary $bin_url magic is '$magic', expected 'HYLT'"

  # 2. Test standard HTML render
  hdr_html=$(curl -s -D - -o /dev/null "$BASE$url")
  printf '%s' "$hdr_html" | grep -qi "HTTP/1.1 200" || fail "HTML $url did not return 200 OK"
  printf '%s' "$hdr_html" | grep -qi "content-type: text/html" || fail "HTML $url is not text/html"

  pass "$module: sort by $col ($dir) OK (binary + HTML)"
}

# Song table: title, type, author
test_sort "song" "title" "asc"
test_sort "song" "title" "desc"
test_sort "song" "author" "asc"
test_sort "song" "author" "desc"
test_sort "song" "type" "asc"
test_sort "song" "type" "desc"

# Poem table: title, owner
test_sort "poem" "title" "asc"
test_sort "poem" "title" "desc"
test_sort "poem" "owner" "asc"
test_sort "poem" "owner" "desc"

# Gig table: title, grp
test_sort "gig" "title" "asc"
test_sort "gig" "title" "desc"
test_sort "gig" "grp" "asc"
test_sort "gig" "grp" "desc"

# Grp table: title
test_sort "grp" "title" "asc"
test_sort "grp" "title" "desc"

echo "ALL MULTI-COLUMN TABLE SORTING INTEGRATION TESTS PASSED"
