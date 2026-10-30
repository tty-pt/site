#!/bin/bash
# 07-nd-move-refresh.sh -- moving in /nd must refresh the client, not only the
# terminal. After every vim move the live WebSocket stream must carry, as
# separate frames: the new room (BCP ITEM, dynflags=1), BCP VIEW and a
# BCP VIEW_BUFFER that differs from the previous map. The mover's own text must
# not echo (observers need a second authenticated account, which this harness
# cannot provision; they use the same eng_nd_rwrite path as `say`) "<me> goes north." / "comes in from", and standing up reads "You stand up."
set -u

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
AXIL_BIN="$REPO_ROOT/external/axil/bin/axil"
[ -x "$AXIL_BIN" ] || AXIL_BIN=axil
TMP="$(mktemp -d /tmp/nd-moverefresh-XXXXXX)"
PID=""
cleanup() {
	[ -n "$PID" ] && { kill -TERM "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; }
	rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

. "$REPO_ROOT/scripts/nd-kernel-mods.sh"
export PATH="$REPO_ROOT/external/axil/bin:$PATH"
export LD_LIBRARY_PATH="$(nd_local_ld_path "$REPO_ROOT")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export AXIL_ND_HTDOCS="$REPO_ROOT/htdocs"

db="$TMP/u.db"; log="$TMP/axil.log"; port=$((29900 + RANDOM % 90))
bash "$REPO_ROOT/scripts/seed-nd-universe.sh" "$db" 1 > "$TMP/seed.log" 2>&1 \
	|| { echo "seeding failed"; tail -5 "$TMP/seed.log"; exit 2; }
(cd "$REPO_ROOT" && AXIL_ND_DB="$db" exec "$AXIL_BIN" -A -C "$REPO_ROOT" -p "$port" -d \
	-m "mods/core/core:$ND_KERNEL_MODS" > "$log" 2>&1) &
PID=$!
tries=100
until grep -q "st_init: st_restore: region id=0x0000000000000000" "$log" 2>/dev/null; do
	tries=$((tries - 1)); [ $tries -gt 0 ] || { echo "daemon did not start"; tail "$log"; exit 2; }
	sleep 0.1
done

python3 - "$port" <<'PY'
import base64, re, socket, sys, time
port = int(sys.argv[1])
fails = []

class Ws:
    def __init__(self):
        self.s = socket.create_connection(("127.0.0.1", port))
        self.s.sendall(("GET /nd HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n"
                        % base64.b64encode(b"1234567890123456").decode()).encode())
        self.buf = b""
        self.hdr = False
    def pump(self, wait):
        self.s.settimeout(0.3)
        end = time.time() + wait
        while time.time() < end:
            try:
                d = self.s.recv(65536)
                if not d:
                    break
                self.buf += d
            except socket.timeout:
                pass
    def messages(self):
        """Complete WebSocket messages received since the last call: [(is_text, bytes)]."""
        if not self.hdr:
            i = self.buf.find(b"\r\n\r\n")
            if i < 0:
                return []
            self.buf = self.buf[i + 4:]
            self.hdr = True
        out = []
        while len(self.buf) >= 2:
            b = self.buf
            op, ln, i = b[0] & 15, b[1] & 127, 2
            if ln == 126:
                if len(b) < 4: break
                ln, i = int.from_bytes(b[2:4], "big"), 4
            elif ln == 127:
                if len(b) < 10: break
                ln, i = int.from_bytes(b[2:10], "big"), 10
            if len(b) < i + ln:
                break
            out.append((op == 1, b[i:i + ln]))
            self.buf = b[i + ln:]
        return out
    def send(self, text):
        b, m = text.encode(), b"\x12\x34\x56\x78"
        self.s.sendall(bytes([0x81, 0x80 | len(b)]) + m + bytes(c ^ m[i % 4] for i, c in enumerate(b)))
    def step(self, text, wait=1.0):
        self.messages()
        if text:
            self.send(text)
        self.pump(wait)
        return self.messages()

def bcp(msgs, iden):
    return [p for _, p in msgs if p[:2] == b"#b" and len(p) > 2 and p[2] == iden]
def plain(msgs):
    return b"".join(p for _, p in msgs if p[:2] != b"#b").replace(b"\x1b", b"")

import os
dump = os.environ.get("ND_FRAME_DUMP")
def dump_frames(name, msgs):
    """ND_FRAME_DUMP=dir regenerates tests/fixtures/nd-move/*.hex (one BCP frame per line)."""
    if dump:
        os.makedirs(dump, exist_ok=True)
        with open(os.path.join(dump, name + ".hex"), "w") as f:
            for _, p in msgs:
                if p[:2] == b"#b":
                    f.write(p.hex() + "\n")

me = Ws()
login = me.step(None, 1.5)
dump_frames("login", login)
vb0 = bcp(login, 2)
if not vb0:
    fails.append("login stream has no BCP VIEW_BUFFER frame")
prev = vb0[-1] if vb0 else b""

for key, word in (("k", b"north"), ("j", b"south")):
    msgs = me.step(key + "\n", 1.2)
    dump_frames("move-" + key, msgs)
    txt = plain(msgs)
    if word not in txt:
        fails.append("%s: no 'You go %s' text: %r" % (key, word.decode(), txt[:120]))
    item_rooms = [p for p in bcp(msgs, 0)]
    if not item_rooms:
        fails.append("%s: no BCP ITEM (room) frame after move" % key)
    if not bcp(msgs, 1):
        fails.append("%s: no BCP VIEW frame after move" % key)
    vb = bcp(msgs, 2)
    if not vb:
        fails.append("%s: no BCP VIEW_BUFFER frame after move" % key)
    elif vb[-1] == prev:
        fails.append("%s: VIEW_BUFFER identical to previous map" % key)
    else:
        prev = vb[-1]
    if re.search(rb"\w+ goes " + word, txt) or re.search(rb"\w+ comes in from", txt):
        fails.append("%s: mover is told about their own move: %r" % (key, txt[:200]))

me.step("sit\n", 0.6)
st = plain(me.step("stand\n", 0.8))
if b"You stand up" not in st:
    fails.append("stand: want 'You stand up', got %r" % st[:120])
if b"You stands" in st:
    fails.append("stand: third-person verb in first person text")

if fails:
    print("FAIL: " + "; ".join(fails))
    sys.exit(1)
print("ok: moves emit ROOM+VIEW+changed VIEW_BUFFER, no self-echo, 'You stand up'")
PY
rc=$?
[ $rc -eq 0 ] && echo "PASS: 07-nd-move-refresh"
exit $rc
