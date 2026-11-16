#!/bin/sh
set -e

BASE="http://localhost:8080"

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "PASS: $1"; }

echo "=== Testing mods/auren ==="

# 1. Page GET /auren
echo -n "1. GET /auren... "
res=$(curl -s -i "$BASE/auren")
echo "$res" | grep -q "200 OK" || fail "expected 200 OK"
echo "$res" | grep -q "Auren In-Browser LLM Demo" || fail "missing Auren title in HTML"
echo "$res" | grep -q "auren-container" || fail "missing auren-container in HTML"
echo "$res" | grep -q "/auren-client.js" || fail "missing auren-client.js script tag"
echo "$res" | grep -q "smollm2" || fail "missing smollm2 option"
echo "$res" | grep -q "qwen" || fail "missing qwen option"
pass "Auren page renders correctly with SSR"

# 2. Redirect GET /auren/
echo -n "2. GET /auren/ redirect... "
code=$(curl -sw "%{http_code}" -o /dev/null "$BASE/auren/")
[ "$code" = "301" ] || fail "expected 301, got $code"
pass "301 redirect verified"

# 3. Static asset GET /auren.css
echo -n "3. GET /auren.css... "
code=$(curl -sw "%{http_code}" -o /dev/null "$BASE/auren.css")
[ "$code" = "200" ] || fail "expected 200 for auren.css, got $code"
pass "auren.css served"

# 4. Static asset GET /auren-client.js
echo -n "4. GET /auren-client.js... "
code=$(curl -sw "%{http_code}" -o /dev/null "$BASE/auren-client.js")
[ "$code" = "200" ] || fail "expected 200 for auren-client.js, got $code"
pass "auren-client.js served"

# 5. WebAssembly engine GET /vendor/auren/wllama/wllama.wasm
echo -n "5. GET /vendor/auren/wllama/wllama.wasm... "
code=$(curl -s -I "$BASE/vendor/auren/wllama/wllama.wasm" | grep -i "HTTP/" | awk '{print $2}')
[ "$code" = "200" ] || fail "expected 200 for wllama.wasm, got $code"
pass "wllama.wasm served"

# 6. GGUF model asset GET /models/smollm2-135m-instruct-q4_k_m.gguf
echo -n "6. HEAD /models/smollm2-135m-instruct-q4_k_m.gguf... "
code=$(curl -s -I "$BASE/models/smollm2-135m-instruct-q4_k_m.gguf" | grep -i "HTTP/" | awk '{print $2}')
[ "$code" = "200" ] || fail "expected 200 for smollm2 GGUF, got $code"
pass "smollm2 GGUF model served"

# 7. GGUF model asset GET /models/qwen2.5-0.5b-instruct.gguf
echo -n "7. HEAD /models/qwen2.5-0.5b-instruct.gguf... "
code=$(curl -s -I "$BASE/models/qwen2.5-0.5b-instruct.gguf" | grep -i "HTTP/" | awk '{print $2}')
[ "$code" = "200" ] || fail "expected 200 for qwen GGUF, got $code"
pass "qwen GGUF model served"

echo "=== All mods/auren tests PASS ==="
