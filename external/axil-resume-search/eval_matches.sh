#!/bin/bash
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
AXIL_BIN="/home/quirinpa/site/external/axil/bin/axil"
if [ ! -x "$AXIL_BIN" ]; then
    AXIL_BIN="axil"
fi

MODULE_PATH="$DIR/lib/libaxil-resume-search"
if [ ! -f "$MODULE_PATH.so" ]; then
    echo "Building axil-resume-search module..."
    make -C "$DIR"
fi

# Ensure test resumes exist
RESUME_DIR="$DIR/test_resumes"
if [ ! -f "$RESUME_DIR/paulo_lead_frontend_systems_architect.pdf" ]; then
    echo "Generating test resumes..."
    python3 "$DIR/tests/generate_test_resumes.py" "$RESUME_DIR"
fi

# Check axil-auren daemon
AUREN_CHECK=$(curl -s http://127.0.0.1:4242/v1/embeddings \
    -H "Content-Type: application/json" \
    -d '{"model":"bge-small","input":"ping"}' 2>/dev/null || true)

if ! echo "$AUREN_CHECK" | grep -q "embedding"; then
    echo "FAIL: axil-auren embedding daemon is not responding on 127.0.0.1:4242"
    exit 1
fi

EVAL_PORT=${EVAL_PORT:-18975}
EVAL_UPLOAD_DIR="/tmp/resume_eval_uploads_$$"
mkdir -p "$EVAL_UPLOAD_DIR"

echo "=========================================================================="
echo "⚡ Starting Axil Resume Semantic Matching Evaluation (Port $EVAL_PORT)"
echo "=========================================================================="

RESUME_UPLOAD_DIR="$EVAL_UPLOAD_DIR" "$AXIL_BIN" -d -p "$EVAL_PORT" -m "$MODULE_PATH" >/tmp/axil_eval.log 2>&1 &
SERVER_PID=$!

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    rm -rf "$EVAL_UPLOAD_DIR"
}
trap cleanup EXIT INT TERM

# Wait for server readiness
for i in {1..30}; do
    if nc -z 127.0.0.1 "$EVAL_PORT" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

if ! nc -z 127.0.0.1 "$EVAL_PORT" 2>/dev/null; then
    echo "FAIL: server failed to start on port $EVAL_PORT"
    cat /tmp/axil_eval.log
    exit 1
fi

BASE_URL="http://127.0.0.1:$EVAL_PORT"

# Ingest all 6 test resumes
echo ""
echo "--- Ingesting 6 Test Resumes via POST /resume ---"
for pdf in "$RESUME_DIR"/*.pdf; do
    fn=$(basename "$pdf")
    echo "Uploading $fn..."
    RESP=$(curl -s -X POST "$BASE_URL/resume" -F "file=@$pdf;type=application/pdf")
    if ! echo "$RESP" | python3 -c "import sys, json; d=json.load(sys.stdin); sys.exit(0 if d.get('ok') else 1)"; then
        echo "FAIL: failed to ingest $fn: $RESP"
        exit 1
    fi
    ref=$(echo "$RESP" | python3 -c "import sys, json; print(json.load(sys.stdin)['ref'])")
    cand=$(echo "$RESP" | python3 -c "import sys, json; print(json.load(sys.stdin)['candidate'])")
    echo "  -> Ingested Ref #$ref: '$cand'"
done

echo ""
echo "--- Running Semantic Matching Against Job Opportunity Prompts ---"
echo ""

PROMPTS_FILE="$DIR/test_prompts.json"

python3 - << EOF
import sys, json, urllib.parse, urllib.request

base_url = "$BASE_URL"
with open("$PROMPTS_FILE", "r") as f:
    prompts = json.load(f)

print(f"{'#':<3} | {'Job Opportunity Prompt':<42} | {'Target Candidate':<18} | {'Top Match #1':<18} | {'Score':<7} | {'Result'}")
print("-" * 115)

all_passed = True
for idx, item in enumerate(prompts, 1):
    job_id = item['id']
    job_title = item['title'][:40]
    target = item['target_candidate']
    prompt_text = item['prompt']

    url = f"{base_url}/candidates?job={urllib.parse.quote(prompt_text)}&mode=hybrid&k=6"
    req = urllib.request.urlopen(url)
    data = json.loads(req.read().decode('utf-8'))

    candidates = data.get('candidates', [])
    top_cand = candidates[0]['candidate'] if candidates else "NONE"
    top_score = candidates[0]['score'] if candidates else 0.0

    # Check if target candidate matches top match
    target_clean = target.lower().split()[0]
    matched = target_clean in top_cand.lower()

    res_str = "✓ PASS" if matched else "✗ FAIL"
    if not matched:
        all_passed = False

    print(f"{idx:<3} | {job_title:<42} | {target:<18} | {top_cand[:18]:<18} | {top_score*100:>5.1f}% | {res_str}")

print("-" * 115)
if all_passed:
    print("ALL 6 JOB OPPORTUNITY TEST PROMPTS MATCHED EXPECTED CANDIDATES WITH 100% ACCURACY!")
    sys.exit(0)
else:
    print("SOME MATCHES FAILED ACCURACY VERIFICATION!")
    sys.exit(1)
EOF

echo ""
echo "Evaluation completed successfully."
