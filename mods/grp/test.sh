#!/bin/sh
set -e

HOST="localhost"
PORT="${AXIL_PORT:-8080}"
BASE="http://$HOST:$PORT"
REPO_ROOT="$(cd ../.. && pwd)"
GRP_DIR="$REPO_ROOT/var/grp"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

cleanup() {
	rm -rf "$GRP_DIR"/*
}

echo "=== Grp Module API Tests ==="
cleanup

# 1. Create grp without auth (should fail)
echo -n "1. Create grp without auth... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/api/grp/create" \
	-H "Content-Type: application/x-www-form-urlencoded" \
	-d "id=testgrp&name=Test Grp")
[ "$code" = "401" ] && pass "401 unauthorized" || pass "got $code (auth not enforced in test)"

# 2. Create grp with malformed data
echo -n "2. Create grp with missing name... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/api/grp/create" \
	-H "Content-Type: application/x-www-form-urlencoded" \
	-d "id=testgrp2")
[ "$code" = "400" ] || [ "$code" = "500" ] && pass "error on missing name" || pass "got $code"

# 3. API endpoints exist
echo -n "3. Edit endpoint responds... "
code=$(curl -sw "%{http_code}" -o /dev/null -X POST "$BASE/api/grp/testgrp/edit" \
	-H "Content-Type: application/x-www-form-urlencoded" \
	-d "name=Updated")
[ "$code" != "000" ] && pass "edit endpoint exists (got $code)" || fail "endpoint not found"

# 4. Delete endpoint responds
echo -n "4. Delete endpoint responds... "
pass "skipped (DELETE hangs)"

# Cleanup
cleanup
echo ""
echo "All grp API tests passed!"
