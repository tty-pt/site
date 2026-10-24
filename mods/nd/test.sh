#!/bin/sh
set -e

REPO_ROOT="$(cd ../.. && pwd)"
echo "=== Running mods/nd test suite ==="

# 1. Run BCP protocol decoder unit tests
"$REPO_ROOT/tests/unit/run-nd-bcp.sh"

# 2. Check that nd.so and nd.wasm exist
[ -f "$REPO_ROOT/mods/nd/nd.so" ] || { echo "FAIL: nd.so missing"; exit 1; }
[ -f "$REPO_ROOT/htdocs/nd.wasm" ] || { echo "FAIL: nd.wasm missing"; exit 1; }
echo "PASS: nd.so and nd.wasm present"

# 3. If server is running on :8080, smoke test GET /nd
if curl -s --max-time 1 http://localhost:8080/ >/dev/null 2>&1; then
	code=$(curl -sw "%{http_code}" -o /tmp/nd_page_test.html "http://localhost:8080/nd")
	if [ "$code" = "200" ]; then
		grep -q "bud-root" /tmp/nd_page_test.html && echo "PASS: /nd renders bud-root" || { echo "FAIL: bud-root missing in /nd"; exit 1; }
	fi
fi

echo "ALL ND TESTS PASSED"
exit 0
