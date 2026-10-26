#!/bin/bash
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
AXIL_BIN="/home/quirinpa/site/external/axil/bin/axil"
if [ ! -x "$AXIL_BIN" ]; then
    AXIL_BIN="axil"
fi

echo "=== axil-embed-search Test Suite ==="

# 1. Build module
echo "--- Step 1: Building axil-embed-search module ---"
make -C "$DIR" clean >/dev/null 2>&1 || true
make -C "$DIR"

MODULE_PATH="$DIR/lib/libaxil-embed-search"
if [ ! -f "$MODULE_PATH.so" ]; then
    echo "FAIL: $MODULE_PATH.so was not built!"
    exit 1
fi

# 2. Check axil-auren daemon
echo "--- Step 2: Checking axil-auren embedding daemon on :4242 ---"
AUREN_CHECK=$(curl -s http://127.0.0.1:4242/v1/embeddings \
    -H "Content-Type: application/json" \
    -d '{"model":"bge-small","input":"ping"}' 2>/dev/null || true)

if ! echo "$AUREN_CHECK" | grep -q "embedding"; then
    echo "FAIL: axil-auren embedding daemon is not responding on 127.0.0.1:4242"
    echo "Response was: $AUREN_CHECK"
    exit 1
fi
echo "axil-auren embedding daemon is healthy."

# 3. Start ephemeral axil server
TEST_PORT=${TEST_PORT:-18950}
echo "--- Step 3: Starting ephemeral axil server on port $TEST_PORT ---"

"$AXIL_BIN" -d -p "$TEST_PORT" -m "$MODULE_PATH" >/tmp/axil_embed_test.log 2>&1 &
SERVER_PID=$!

cleanup() {
    echo "Cleaning up server PID $SERVER_PID..."
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Wait for server to listen
for i in {1..20}; do
    if nc -z 127.0.0.1 "$TEST_PORT" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

if ! nc -z 127.0.0.1 "$TEST_PORT" 2>/dev/null; then
    echo "FAIL: axil server failed to start on port $TEST_PORT"
    cat /tmp/axil_embed_test.log
    exit 1
fi
echo "axil server is ready on port $TEST_PORT."

BASE_URL="http://127.0.0.1:$TEST_PORT"

# 4. Test Error Handling on POST /embeddings
echo "--- Step 4: Testing POST /embeddings error handling ---"

# Empty body -> 400
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$BASE_URL/embeddings")
if [ "$HTTP_CODE" != "400" ]; then
    echo "FAIL: expected 400 for empty body, got $HTTP_CODE"
    exit 1
fi
echo "PASS: empty body correctly rejected with 400."

# Invalid JSON -> 400
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$BASE_URL/embeddings" \
    -H "Content-Type: application/json" -d "not-a-json")
if [ "$HTTP_CODE" != "400" ]; then
    echo "FAIL: expected 400 for invalid JSON, got $HTTP_CODE"
    exit 1
fi
echo "PASS: invalid JSON correctly rejected with 400."

# Missing prompt -> 400
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$BASE_URL/embeddings" \
    -H "Content-Type: application/json" -d '{"ref":1}')
if [ "$HTTP_CODE" != "400" ]; then
    echo "FAIL: expected 400 for missing prompt, got $HTTP_CODE"
    exit 1
fi
echo "PASS: missing prompt field correctly rejected with 400."

# 5. Ingest Prompts via POST /embeddings
echo "--- Step 5: Ingesting prompts via POST /embeddings ---"

# Document 1
RESP1=$(curl -s -X POST "$BASE_URL/embeddings" \
    -H "Content-Type: application/json" \
    -d '{"prompt":"High-performance vector similarity search in pure C","ref":101}')
echo "Doc 1 response: $RESP1"
if ! echo "$RESP1" | grep -q '"ok":[[:space:]]*true' || ! echo "$RESP1" | grep -q '"ref":[[:space:]]*101' || ! echo "$RESP1" | grep -q '"dim":[[:space:]]*384'; then
    echo "FAIL: unexpected response for Doc 1: $RESP1"
    exit 1
fi
echo "PASS: Doc 1 stored with ref 101, dim 384."

# Document 2 (with Portuguese accents)
RESP2=$(curl -s -X POST "$BASE_URL/embeddings" \
    -H "Content-Type: application/json" \
    -d '{"prompt":"Traditional Portuguese bakery baking warm pão de queijo","ref":102}')
echo "Doc 2 response: $RESP2"
if ! echo "$RESP2" | grep -q '"ok":[[:space:]]*true' || ! echo "$RESP2" | grep -q '"ref":[[:space:]]*102' || ! echo "$RESP2" | grep -q '"dim":[[:space:]]*384'; then
    echo "FAIL: unexpected response for Doc 2: $RESP2"
    exit 1
fi
echo "PASS: Doc 2 stored with ref 102, dim 384."

# Document 3 (auto-increment ref, using 'text' key)
RESP3=$(curl -s -X POST "$BASE_URL/api/embeddings" \
    -H "Content-Type: application/json" \
    -d '{"text":"Zero-copy in-memory caching and RAM table indexing"}')
echo "Doc 3 response: $RESP3"
if ! echo "$RESP3" | grep -q '"ok":[[:space:]]*true' || ! echo "$RESP3" | grep -q '"dim":[[:space:]]*384'; then
    echo "FAIL: unexpected response for Doc 3: $RESP3"
    exit 1
fi
echo "PASS: Doc 3 stored with auto-increment ref, dim 384."

# 6. Test Error Handling on GET /search
echo "--- Step 6: Testing GET /search error handling ---"

HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/search")
if [ "$HTTP_CODE" != "400" ]; then
    echo "FAIL: expected 400 for missing query, got $HTTP_CODE"
    exit 1
fi
echo "PASS: missing query correctly rejected with 400."

# 7. Semantic Search (mode=sepal)
echo "--- Step 7: Testing Semantic Search (mode=sepal) ---"

SEARCH_RESP=$(curl -s "$BASE_URL/search?q=fast+memory+storage+techniques&mode=sepal")
echo "Semantic search response: $SEARCH_RESP"
if ! echo "$SEARCH_RESP" | grep -q '"ok":[[:space:]]*true' || ! echo "$SEARCH_RESP" | grep -q '"mode":[[:space:]]*"sepal"'; then
    echo "FAIL: semantic search failed: $SEARCH_RESP"
    exit 1
fi
if ! echo "$SEARCH_RESP" | grep -q "Zero-copy"; then
    echo "FAIL: semantic search did not match expected document: $SEARCH_RESP"
    exit 1
fi
echo "PASS: Semantic search found conceptually matching document."

# 8. Lexical Search (mode=lemma)
echo "--- Step 8: Testing Lexical Search (mode=lemma) ---"

LEMMA_RESP=$(curl -s "$BASE_URL/search?q=pão&mode=lemma")
echo "Lexical search response: $LEMMA_RESP"
if ! echo "$LEMMA_RESP" | grep -q '"ok":[[:space:]]*true' || ! echo "$LEMMA_RESP" | grep -q '"mode":[[:space:]]*"lemma"' || ! echo "$LEMMA_RESP" | grep -q "102"; then
    echo "FAIL: lexical search failed: $LEMMA_RESP"
    exit 1
fi
echo "PASS: Lexical search found accent-sensitive token match (pão)."

# 9. Hybrid Search (mode=hybrid)
echo "--- Step 9: Testing Hybrid Search (mode=hybrid) ---"

HYBRID_RESP=$(curl -s "$BASE_URL/search?q=baking+fresh+bread+in+portugal&mode=hybrid")
echo "Hybrid search response: $HYBRID_RESP"
if ! echo "$HYBRID_RESP" | grep -q '"ok":[[:space:]]*true' || ! echo "$HYBRID_RESP" | grep -q '"mode":[[:space:]]*"hybrid"' || ! echo "$HYBRID_RESP" | grep -q "102"; then
    echo "FAIL: hybrid search failed: $HYBRID_RESP"
    exit 1
fi
echo "PASS: Hybrid search ranked expected document with combined score."

# 10. API Route Aliases
echo "--- Step 10: Testing Route Aliases (/api/search) ---"

ALIAS_RESP=$(curl -s "$BASE_URL/api/search?q=pure+C+vector&mode=sepal")
echo "Alias search response: $ALIAS_RESP"
if ! echo "$ALIAS_RESP" | grep -q '"ok":[[:space:]]*true' || ! echo "$ALIAS_RESP" | grep -q "101"; then
    echo "FAIL: /api/search alias failed: $ALIAS_RESP"
    exit 1
fi
echo "PASS: /api/search alias verified."

# 11. Web UI Form
echo "--- Step 11: Testing Web UI Form (GET / and GET /embed-search) ---"

UI_RESP=$(curl -s "$BASE_URL/")
if ! echo "$UI_RESP" | grep -q '<title>Axil Embed Search</title>' || ! echo "$UI_RESP" | grep -q 'id="ingest-form"' || ! echo "$UI_RESP" | grep -q 'id="search-form"'; then
    echo "FAIL: GET / did not return expected web UI form: $UI_RESP"
    exit 1
fi
echo "PASS: GET / serves interactive HTML form."

UI_ALIAS=$(curl -s "$BASE_URL/embed-search")
if ! echo "$UI_ALIAS" | grep -q 'id="ingest-form"'; then
    echo "FAIL: GET /embed-search did not return web UI form: $UI_ALIAS"
    exit 1
fi
echo "PASS: GET /embed-search alias verified."

echo "=== All 11 verification steps PASSED successfully! ==="
exit 0
