#!/bin/bash
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
AXIL_BIN="/home/quirinpa/site/external/axil/bin/axil"
if [ ! -x "$AXIL_BIN" ]; then
    AXIL_BIN="axil"
fi

echo "=== axil-resume-search Test Suite ==="

# 1. Build module
echo "--- Step 1: Building axil-resume-search module ---"
make -C "$DIR" clean >/dev/null 2>&1 || true
make -C "$DIR"

MODULE_PATH="$DIR/lib/libaxil-resume-search"
if [ ! -f "$MODULE_PATH.so" ]; then
    echo "FAIL: $MODULE_PATH.so was not built!"
    exit 1
fi
echo "Module built successfully at $MODULE_PATH.so"

# 2. Check prerequisites: pdftotext & axil-auren daemon
echo "--- Step 2: Checking prerequisites ---"
if ! command -v pdftotext >/dev/null 2>&1; then
    echo "FAIL: pdftotext command not found in PATH"
    exit 1
fi
echo "pdftotext found: $(pdftotext -v 2>&1 | head -n 1)"

AUREN_CHECK=$(curl -s http://127.0.0.1:4242/v1/embeddings \
    -H "Content-Type: application/json" \
    -d '{"model":"bge-small","input":"ping"}' 2>/dev/null || true)

if ! echo "$AUREN_CHECK" | grep -q "embedding"; then
    echo "FAIL: axil-auren embedding daemon is not responding on 127.0.0.1:4242"
    echo "Response was: $AUREN_CHECK"
    exit 1
fi
echo "axil-auren embedding daemon is healthy on port 4242."

# 3. Generate sample PDFs
echo "--- Step 3: Generating test resume PDFs ---"
python3 "$DIR/tests/generate_sample_resumes.py"

ALICE_PDF="$DIR/tests/alice_systems_engineer.pdf"
BOB_PDF="$DIR/tests/bob_data_scientist.pdf"
CAROL_PDF="$DIR/tests/carol_graphic_designer.pdf"

if [ ! -f "$ALICE_PDF" ] || [ ! -f "$BOB_PDF" ] || [ ! -f "$CAROL_PDF" ]; then
    echo "FAIL: sample resume PDFs were not created!"
    exit 1
fi
echo "Sample PDFs verified."

# 4. Start ephemeral axil server
TEST_PORT=${TEST_PORT:-18962}
echo "--- Step 4: Starting ephemeral axil server on port $TEST_PORT ---"

TEST_UPLOAD_DIR="/tmp/resume_test_uploads_$$"
mkdir -p "$TEST_UPLOAD_DIR"

RESUME_UPLOAD_DIR="$TEST_UPLOAD_DIR" "$AXIL_BIN" -d -p "$TEST_PORT" -m "$MODULE_PATH" >/tmp/axil_resume_test.log 2>&1 &
SERVER_PID=$!

cleanup() {
    echo "Cleaning up server PID $SERVER_PID and temporary files..."
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    rm -rf "$TEST_UPLOAD_DIR"
}
trap cleanup EXIT INT TERM

# Wait for server to listen
for i in {1..30}; do
    if nc -z 127.0.0.1 "$TEST_PORT" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

if ! nc -z 127.0.0.1 "$TEST_PORT" 2>/dev/null; then
    echo "FAIL: axil server failed to start on port $TEST_PORT"
    cat /tmp/axil_resume_test.log
    exit 1
fi
echo "axil server is ready on port $TEST_PORT."

BASE_URL="http://127.0.0.1:$TEST_PORT"

# 5. Test Error Handling
echo "--- Step 5: Testing error handling ---"

# Empty body -> 400
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$BASE_URL/resume")
if [ "$HTTP_CODE" != "400" ]; then
    echo "FAIL: expected 400 for empty body, got $HTTP_CODE"
    exit 1
fi
echo "PASS: empty body correctly rejected with 400."

# Non-PDF text -> 400 (missing %PDF- header)
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$BASE_URL/resume" \
    -H "Content-Type: application/pdf" \
    --data-binary "This is not a PDF file")
if [ "$HTTP_CODE" != "400" ]; then
    echo "FAIL: expected 400 for non-PDF payload, got $HTTP_CODE"
    exit 1
fi
echo "PASS: non-PDF payload rejected with 400."

# Missing query on GET /candidates -> 400
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/candidates")
if [ "$HTTP_CODE" != "400" ]; then
    echo "FAIL: expected 400 for missing job query, got $HTTP_CODE"
    exit 1
fi
echo "PASS: missing job query correctly rejected with 400."

# 6. Ingest Resumes via POST /resume
echo "--- Step 6: Ingesting candidate resumes via POST /resume ---"

# Ingest 1: Alice Smith via multipart/form-data
echo "Uploading Candidate 1 (Alice Smith) via multipart/form-data..."
RESP1=$(curl -s -X POST "$BASE_URL/resume" \
    -F "candidate=Alice Smith - Systems Engineer" \
    -F "file=@$ALICE_PDF;type=application/pdf")

echo "Upload 1 response: $RESP1"
if ! echo "$RESP1" | python3 -c "import sys, json; d=json.load(sys.stdin); sys.exit(0 if d.get('ok') else 1)"; then
    echo "FAIL: Candidate 1 ingestion failed!"
    exit 1
fi
REF1=$(echo "$RESP1" | python3 -c "import sys, json; print(json.load(sys.stdin)['ref'])")
FILE_PATH1=$(echo "$RESP1" | python3 -c "import sys, json; print(json.load(sys.stdin)['file_path'])")
if [ ! -f "$FILE_PATH1" ]; then
    echo "FAIL: Physical PDF file was not stored on disk at $FILE_PATH1"
    exit 1
fi
echo "PASS: Alice Smith ingested as ref $REF1 (stored at $FILE_PATH1)."

# Ingest 2: Bob Jones via raw binary application/pdf
echo "Uploading Candidate 2 (Bob Jones) via raw binary POST..."
RESP2=$(curl -s -X POST "$BASE_URL/resume?candidate=Bob+Jones+-+Data+Scientist&filename=bob_resume.pdf" \
    -H "Content-Type: application/pdf" \
    --data-binary "@$BOB_PDF")

echo "Upload 2 response: $RESP2"
if ! echo "$RESP2" | python3 -c "import sys, json; d=json.load(sys.stdin); sys.exit(0 if d.get('ok') else 1)"; then
    echo "FAIL: Candidate 2 ingestion failed!"
    exit 1
fi
REF2=$(echo "$RESP2" | python3 -c "import sys, json; print(json.load(sys.stdin)['ref'])")
FILE_PATH2=$(echo "$RESP2" | python3 -c "import sys, json; print(json.load(sys.stdin)['file_path'])")
if [ ! -f "$FILE_PATH2" ]; then
    echo "FAIL: Physical PDF file was not stored on disk at $FILE_PATH2"
    exit 1
fi
echo "PASS: Bob Jones ingested as ref $REF2 (stored at $FILE_PATH2)."

# Ingest 3: Carol White via JSON base64
echo "Uploading Candidate 3 (Carol White) via base64 JSON payload..."
B64_DATA=$(base64 -w 0 "$CAROL_PDF")
RESP3=$(curl -s -X POST "$BASE_URL/resume" \
    -H "Content-Type: application/json" \
    -d "{\"candidate\":\"Carol White - UI/UX Designer\",\"filename\":\"carol_design.pdf\",\"pdf_base64\":\"$B64_DATA\"}")

echo "Upload 3 response: $RESP3"
if ! echo "$RESP3" | python3 -c "import sys, json; d=json.load(sys.stdin); sys.exit(0 if d.get('ok') else 1)"; then
    echo "FAIL: Candidate 3 ingestion failed!"
    exit 1
fi
REF3=$(echo "$RESP3" | python3 -c "import sys, json; print(json.load(sys.stdin)['ref'])")
FILE_PATH3=$(echo "$RESP3" | python3 -c "import sys, json; print(json.load(sys.stdin)['file_path'])")
if [ ! -f "$FILE_PATH3" ]; then
    echo "FAIL: Physical PDF file was not stored on disk at $FILE_PATH3"
    exit 1
fi
echo "PASS: Carol White ingested as ref $REF3 (stored at $FILE_PATH3)."

# 7. Test Candidates List
echo "--- Step 7: Testing GET /candidates/list ---"
LIST_RESP=$(curl -s "$BASE_URL/candidates/list")
echo "List response: $LIST_RESP"
LIST_COUNT=$(echo "$LIST_RESP" | python3 -c "import sys, json; print(json.load(sys.stdin).get('count', 0))")
if [ "$LIST_COUNT" -ne 3 ]; then
    echo "FAIL: Expected candidate count to be 3, got $LIST_COUNT!"
    exit 1
fi
echo "PASS: All 3 candidates listed."

# 8. Test Semantic Matching of Candidates to Job Opportunities
echo "--- Step 8: Testing semantic candidate search against job opportunities ---"

# Job 1: C systems engineer
JOB1="Looking for a senior C systems programmer with extensive Linux kernel and TCP/IP networking daemon experience"
echo "Job Query 1: $JOB1"
SEARCH1=$(curl -s "$BASE_URL/candidates?job=$(python3 -c "import urllib.parse; print(urllib.parse.quote('''$JOB1'''))")&mode=hybrid")
echo "Search 1 response: $SEARCH1"

# Check that Alice is ranked #1
TOP_CAND1=$(echo "$SEARCH1" | python3 -c "import sys, json; d=json.load(sys.stdin); print(d['candidates'][0]['candidate'])")
echo "Top match for Job 1: $TOP_CAND1"
if ! echo "$TOP_CAND1" | grep -qi "Alice"; then
    echo "FAIL: Expected Alice Smith to be top match for C systems job, got '$TOP_CAND1'"
    exit 1
fi
echo "PASS: Alice Smith correctly ranked #1 for C systems job opportunity."

# Job 2: Machine learning / Data science
JOB2="Seeking a Machine Learning specialist skilled in PyTorch, transformers, and deep neural network NLP architectures"
echo "Job Query 2: $JOB2"
SEARCH2=$(curl -s "$BASE_URL/candidates?job=$(python3 -c "import urllib.parse; print(urllib.parse.quote('''$JOB2'''))")&mode=hybrid")
echo "Search 2 response: $SEARCH2"

TOP_CAND2=$(echo "$SEARCH2" | python3 -c "import sys, json; d=json.load(sys.stdin); print(d['candidates'][0]['candidate'])")
echo "Top match for Job 2: $TOP_CAND2"
if ! echo "$TOP_CAND2" | grep -qi "Bob"; then
    echo "FAIL: Expected Bob Jones to be top match for ML/NLP job, got '$TOP_CAND2'"
    exit 1
fi
echo "PASS: Bob Jones correctly ranked #1 for ML/NLP job opportunity."

# Job 3: Graphic Design / UI
JOB3="Creative visual product designer experienced in Figma design systems, wireframing, and branding"
echo "Job Query 3: $JOB3"
SEARCH3=$(curl -s "$BASE_URL/candidates?job=$(python3 -c "import urllib.parse; print(urllib.parse.quote('''$JOB3'''))")&mode=hybrid")
echo "Search 3 response: $SEARCH3"

TOP_CAND3=$(echo "$SEARCH3" | python3 -c "import sys, json; d=json.load(sys.stdin); print(d['candidates'][0]['candidate'])")
echo "Top match for Job 3: $TOP_CAND3"
if ! echo "$TOP_CAND3" | grep -qi "Carol"; then
    echo "FAIL: Expected Carol White to be top match for design job, got '$TOP_CAND3'"
    exit 1
fi
echo "PASS: Carol White correctly ranked #1 for UI/UX design job opportunity."

# 9. Test PDF File Download
echo "--- Step 9: Testing PDF file retrieval via GET /resume/file ---"
DL_PATH="/tmp/downloaded_alice_$$.pdf"
HTTP_CODE=$(curl -s -o "$DL_PATH" -w "%{http_code}" "$BASE_URL/resume/file?ref=$REF1")
if [ "$HTTP_CODE" != "200" ]; then
    echo "FAIL: expected 200 downloading PDF for ref $REF1, got $HTTP_CODE"
    exit 1
fi
ORIG_SIZE=$(stat -c%s "$ALICE_PDF")
DL_SIZE=$(stat -c%s "$DL_PATH")
if [ "$ORIG_SIZE" != "$DL_SIZE" ]; then
    echo "FAIL: downloaded file size ($DL_SIZE) does not match original ($ORIG_SIZE)!"
    exit 1
fi
rm -f "$DL_PATH"
echo "PASS: PDF file for ref $REF1 downloaded with exact matching size ($ORIG_SIZE bytes)."

# 10. Test UI Landing Page
echo "--- Step 10: Testing Web UI at GET / ---"
UI_RESP=$(curl -s "$BASE_URL/")
if ! echo "$UI_RESP" | grep -q "Resume Candidate Matcher"; then
    echo "FAIL: UI page did not return expected content"
    exit 1
fi
echo "PASS: Web UI returned 200 OK with expected HTML."

echo "========================================="
echo "ALL TESTS PASSED SUCCESSFULLY! (10/10)"
echo "========================================="
