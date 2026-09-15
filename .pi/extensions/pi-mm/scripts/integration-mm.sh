#!/bin/sh -e
# integration-mm.sh — real-qmap smoke for pi-mm (§8 recipes, joint+stoma).
# Ephemeral dir; builds/uses in-site external/libqmap/bin/qmap + sibling libs.
# Mirrors test-mm.sh structure; exit nonzero on any mismatch.

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../../.." && pwd)
QMAP="$ROOT/external/libqmap/bin/qmap"
JOINT_LIB="$ROOT/external/libjoint/lib"
STOMA_LIB="$ROOT/external/libstoma/lib"
AXIS_PATH="$JOINT_LIB:$STOMA_LIB"
export QMAP_AXIS_PATH="$AXIS_PATH"
export LD_LIBRARY_PATH="$ROOT/external/libqmap/lib:$LD_LIBRARY_PATH"

if [ ! -x "$QMAP" ]; then echo "integration: missing qmap at $QMAP (run make -C external/libqmap)" >&2; exit 1; fi
for a in joint stoma; do
  if [ ! -f "$JOINT_LIB/lib$a.so" ] && [ ! -f "$STOMA_LIB/lib$a.so" ]; then :; fi
done
if [ ! -f "$JOINT_LIB/libjoint.so" ] || [ ! -f "$STOMA_LIB/libstoma.so" ]; then
  echo "integration: missing axis libs under $JOINT_LIB / $STOMA_LIB" >&2
  exit 1
fi

td=$(mktemp -d)
trap 'rm -rf "$td"' EXIT
mem="$td/.pi/mm"
mkdir -p "$mem"
db="$mem/mem.db"
FILESPEC="$mem/mem.db@joint,stoma:a:s"
qmap() { QMAP_AXIS_PATH="$AXIS_PATH" "$QMAP" "$@"; }
qmap_cwd() { qmap "$@"; }

fail=0
ok() { echo "ok - $1"; }
failmsg() { echo "FAIL - $1"; echo "  $2"; fail=1; }

# store three memories (history: explicit refs per §8, F1) — absolute filespec avoids ./ alias clobber
qmap_cwd -p 1:"2026-09-14:Beacon Harbor lights" "$FILESPEC" >/dev/null
qmap_cwd -p 2:"2026-09-14:Beacon AND Pão" "$FILESPEC" >/dev/null
qmap_cwd -p 3:"2026-09-15:Beacon Harbor lights again" "$FILESPEC" >/dev/null

# scan level 0 (pure text): beacon should return 3 (+/- ordering by score/ties asc ref)
out=$(qmap_cwd -X 'stoma="field=text query=beacon matched=1"' -g . "$FILESPEC" -t 10)
echo "$out" | grep -q "^1 " || failmsg "scan level-0 has ref 1" "$out"
echo "$out" | grep -q "^2 " || failmsg "scan level-0 has ref 2" "$out"
echo "$out" | grep -q "^3 " || failmsg "scan level-0 has ref 3" "$out"
[ $fail -eq 0 ] && ok "scan level-0 pure text"

# scan level 0 with --until (all-time up to date): epoch-to-until window
out=$(qmap_cwd -X '(joint="a=0 b=2026-09-15" AND stoma="field=text query=beacon matched=1")' -g . "$FILESPEC" -t 10)
echo "$out" | grep -q "^1 " || failmsg "scan level-0 until=2026-09-15 has ref 1" "$out"
echo "$out" | grep -q "^2 " || failmsg "scan level-0 until=2026-09-15 has ref 2" "$out"
echo "$out" | grep -q "^3 " && failmsg "scan level-0 until=2026-09-15 must NOT have ref 3" "$out" || ok "scan level-0 + until caps at ref 3"

# scan level 1 (today window = 2026-09-15) — need a date that matches 3 only.
# Use a=2026-09-15 b=2026-09-16 and text beacon → should return 3.
out=$(qmap_cwd -X '(joint="a=2026-09-15 b=2026-09-16" AND stoma="field=text query=beacon matched=1")' -g . "$FILESPEC" -t 10)
echo "$out" | grep -q "^3 " || failmsg "scan level-1 today window has ref 3" "$out"
[ $fail -eq 0 ] && ok "scan level-1 today window"

# accent-sensitive: Pão ≠ pao
out_accent=$(qmap_cwd -X 'stoma="field=text query=Pão matched=1"' -g . "$FILESPEC" -t 10)
echo "$out_accent" | grep -q "^2 " || failmsg "accent Pão finds ref 2" "$out_accent"
out_pao=$(qmap_cwd -X 'stoma="field=text query=pao matched=1"' -g . "$FILESPEC" -t 10 2>/dev/null || true)
echo "$out_pao" | grep -q "^2 " && failmsg "accent: pao must not find Pão" "$out_pao" || ok "accent-sensitive Pão≠pao"

# think: get ref 2 raw payload (AINDEX needs -r)
payload=$(qmap_cwd -rg 2 "$FILESPEC")
echo "$payload" | grep -q "Pão" || failmsg "get ref 2 has Pão" "$payload"
[ $fail -eq 0 ] && ok "get ref 2"

# forget ref 2 (roster-backed, idempotent)
qmap_cwd -d 2 "$FILESPEC" >/dev/null
qmap_cwd -d 2 "$FILESPEC" >/dev/null || true
out=$(qmap_cwd -X 'stoma="field=text query=Pão matched=1"' -g . "$FILESPEC" -t 10 2>/dev/null || true)
echo "$out" | grep -q "^2 " && failmsg "forget 2 removed" "$out" || ok "forget idempotent"

# reset: enumerate bare + forget each (§8)
for ref in $(qmap_cwd -g . "$FILESPEC" 2>/dev/null | grep -E '^[0-9]+$' || true); do
  [ "$ref" = "-1" ] && continue
  qmap_cwd -d "$ref" "$FILESPEC" >/dev/null || true
done
left=$(qmap_cwd -g . "$FILESPEC" 2>/dev/null | grep -E '^[0-9]+$' | wc -l | tr -d ' ')
[ "$left" != "0" ] && failmsg "reset left $left refs" "$(qmap_cwd -g . "$FILESPEC" 2>/dev/null)" || ok "reset idempotent"
# rerun reset is no-op
for ref in $(qmap_cwd -g . "$FILESPEC" 2>/dev/null | grep -E '^[0-9]+$' || true); do qmap_cwd -d "$ref" "$FILESPEC" >/dev/null || true; done

# ── embed smoke (requires python3 + libsepal) ── store on a ,sepal roster
# via a one-shot local HTTP mock (OpenAI JSON), then query with a vector.
SEPAL_LIB="$ROOT/external/libsepal/lib"
if command -v python3 >/dev/null 2>&1 && [ -f "$SEPAL_LIB/libsepal.so" ]; then
  EMBED_FILE="$mem/mem-embed.db@joint,stoma,sepal:a:s"
  EMBED_AXES="$JOINT_LIB:$STOMA_LIB:$SEPAL_LIB"
  # query vectors as exact float32 LE (sh printf \xHH is not portable; python3 is gated anyway)
  python3 -c 'import struct; open("'"$td"'/vec.bin","wb").write(struct.pack("<3f", 0.1, 0.2, 0.3))'
  python3 -c 'import struct; open("'"$td"'/vec0.bin","wb").write(struct.pack("<3f", 1.0, 0.0, 0.0))'
  [ "$(wc -c < "$td/vec.bin")" = "12" ] || failmsg "vec.bin must be 12 bytes" "$(wc -c < "$td/vec.bin")"
  [ "$(wc -c < "$td/vec0.bin")" = "12" ] || failmsg "vec0.bin must be 12 bytes" "$(wc -c < "$td/vec0.bin")"
  python3 -c '
import http.server, json
class H(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length", 0)))
        body = json.dumps({"data":[{"embedding":[0.1,0.2,0.3]}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *a):
        pass
http.server.HTTPServer(("127.0.0.1", 8081), H).handle_request()
' &
  SRV_PID=$!
  sleep 0.3
  if QMAP_AXIS_PATH="$EMBED_AXES" QMAP_SEPAL_EMBED_URL=http://127.0.0.1:8081/v1/embeddings QMAP_SEPAL_EMBED_MODEL=test \
      "$QMAP" -p 4:"2026-09-16:embedded lighthouse beacon" "$EMBED_FILE" >/dev/null 2>"$td/embed.err"; then
    wait "$SRV_PID" 2>/dev/null || true
    out=$(QMAP_AXIS_PATH="$EMBED_AXES" "$QMAP" -X "sepal=\"file=$td/vec.bin qdim=3 m=10 min_sim=0.4\"" -g . "$EMBED_FILE" -t 10 2>/dev/null)
    echo "$out" | grep -q "^4 " && ok "embed store + sepal query finds ref 4" || failmsg "sepal must find ref 4" "$out"
    out0=$(QMAP_AXIS_PATH="$EMBED_AXES" "$QMAP" -X "sepal=\"file=$td/vec0.bin qdim=3 m=10 min_sim=0.4\"" -g . "$EMBED_FILE" -t 10 2>/dev/null)
    echo "$out0" | grep -q "^4 " && failmsg "dissimilar vector must not match ref 4" "$out0" || ok "sepal similarity gate"
  else
    wait "$SRV_PID" 2>/dev/null || true
    failmsg "embed store failed (exit nonzero)" "$(cat "$td/embed.err")"
  fi
else
  echo "skip - embed smoke (python3 or libsepal missing)"
fi

[ $fail -eq 0 ] && echo "integration-mm: all green" || { echo "integration-mm: $fail failure(s)" >&2; exit 1; }
