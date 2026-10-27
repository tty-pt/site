#!/bin/bash
# 06-nd-tree-map.sh -- a player opening /nd must see the real map: a named biome
# (not `void`), ANSI biome backgrounds and plant glyphs ("the old map, with the
# trees"), resting at the root spawn (0,0,0,0).
#
# Regression it guards: content modules seeded into a child region (plen=11)
# cannot reach the root kernel's nd_put/nd_get (XY dispatch only walks the
# caller region's subtree), so HD_BIOME stayed on the all-void default map.
# The suite proves its own teeth: a legacy-seeder mutant (modules at plen=11)
# and a bad-spawn mutant (ends at the Citadel (0,0,0,1), planet 1 ocean) must
# both FAIL these assertions.
set -u

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
AXIL_ND_DIR="$REPO_ROOT/external/axil-nd"
AXIL_BIN="$REPO_ROOT/external/axil/bin/axil"
[ -x "$AXIL_BIN" ] || AXIL_BIN=axil
PLANTS="$REPO_ROOT/external/nd-plant/data/plants.dsv"
TMP="$(mktemp -d /tmp/nd-treemap-XXXXXX)"
MUT=""
PID=""

cleanup() {
	[ -n "$PID" ] && { kill -TERM "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; }
	[ -n "$MUT" ] && rm -f $MUT
	rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

. "$REPO_ROOT/scripts/nd-kernel-mods.sh"
export PATH="$REPO_ROOT/external/axil/bin:$PATH"
export LD_LIBRARY_PATH="$(nd_local_ld_path "$REPO_ROOT")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export AXIL_ND_HTDOCS="$REPO_ROOT/htdocs"

# make_mutant NAME SED/PY-less transform: the mutants live beside the seeder
# because it resolves REPO_ROOT from its own path; the seeder's own spawn check
# is removed so the failure must come from THIS test's assertions.
make_mutant() {
	local name="$1" out="$REPO_ROOT/scripts/.mut-$1-$$.sh"
	python3 - "$REPO_ROOT/scripts/seed-nd-universe.sh" "$out" "$name" <<'PY'
import re, sys
src, out, kind = sys.argv[1:4]
s = open(src).read()
# drop the seeder's own post-seed check
s = re.sub(r'echo "Verifying the seeded spawn.*?exit 1; \}\n', '', s, flags=re.S)
if kind == "legacy":
    mods = "libnd-biome libnd-plant libnd-drink libnd-seat libnd-stone libnd-wts"
    s = s.replace("\tlibnd-mob libnd-biome libnd-plant libnd-drink libnd-seat libnd-stone libnd-wts; do", "\tlibnd-mob; do")
    s = s.replace("# Establish central planet 1 region under the 32-planet sector",
                  "for mod in %s; do\n\tcmd \"loadmod $mod 11 world 0\"\ndone\n# Establish central planet 1 region under the 32-planet sector" % mods, 1)
elif kind == "spawn":
    tail = 'cmd "room 0 0 0 0"\ncmd "look"'
    i = s.rindex(tail)
    s = s[:i] + 'cmd "room 0 0 0 1"\ncmd "look"' + s[i + len(tail):]
open(out, "w").write(s)
PY
}

# check_case SEEDER LABEL -> 0 when every assertion holds, 1 otherwise
check_case() {
	local seeder="$1" label="$2" db="$TMP/$2.db" log="$TMP/$2.log" port
	port=$((29100 + RANDOM % 800))
	rm -f "$db" "$db.seed-v3"
	bash "$seeder" "$db" 1 > "$TMP/$label.seed.log" 2>&1 \
		|| { echo "[$label] seeding failed"; tail -5 "$TMP/$label.seed.log"; return 2; }
	(cd "$REPO_ROOT" && AXIL_ND_DB="$db" exec "$AXIL_BIN" -A -C "$REPO_ROOT" -p "$port" -d \
		-m "mods/core/core:$ND_KERNEL_MODS" > "$log" 2>&1) &
	PID=$!
	local tries=100
	until grep -q "st_init: st_restore: region id=0x0000000000000000" "$log" 2>/dev/null; do
		tries=$((tries - 1)); [ $tries -gt 0 ] || { echo "[$label] daemon did not start"; tail "$log"; return 2; }
		sleep 0.1
	done
	python3 - "$port" "$PLANTS" "$label" <<'PY'
import base64, re, socket, sys, time
port, plants, label = int(sys.argv[1]), sys.argv[2], sys.argv[3]

glyphs = set()
for line in open(plants, encoding="utf-8"):
    f = line.strip().split("|")
    if line.startswith("#") or len(f) < 6:
        continue
    glyphs.update(g[0] for g in (f[4], f[5]) if g)

s = socket.create_connection(("127.0.0.1", port))
s.sendall(("GET /nd HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
           "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n"
           % base64.b64encode(b"1234567890123456").decode()).encode())

def drain(wait):
    s.settimeout(0.4)
    end, buf = time.time() + wait, b""
    while time.time() < end:
        try:
            d = s.recv(65536)
            if not d:
                break
            buf += d
        except socket.timeout:
            pass
    return buf

def send(text):
    b, m = text.encode(), b"\x12\x34\x56\x78"
    s.sendall(bytes([0x81, 0x80 | len(b)]) + m + bytes(c ^ m[i % 4] for i, c in enumerate(b)))

login = drain(1.5)
if label == "real":
    has_plant_login = any(p in login for p in [b"plant/", b"pinus", b"pseudotsuga", b"betula", b"linum", b"abies", b"acacia"])
    if not has_plant_login:
        fails.append("login stream did not contain plant/tree objects for spawn room")
send("room 0 0 0 0\n")
reply = drain(1.0)
fails = []

m = re.search(rb"([A-Za-z_][A-Za-z_ ]*)\x00+\1\(#(\d+)\)\x00+biome/", login)
if not m:
    fails.append("no room frame in the login stream")
else:
    name, ref = m.group(1).decode(), m.group(2).decode()
    if name == "void":
        fails.append("room is named `void`: biome map is the all-void default")
    r = re.search(rb"room (\d+) at (-?\d+) (-?\d+) (-?\d+) (-?\d+) ", reply)
    if not r:
        fails.append("no `room N at ...` reply to `room 0 0 0 0`")
    elif b" ".join(r.groups()[1:]) != b"0 0 0 0" or r.group(1).decode() != ref:
        fails.append("player rests in #%s, spawn (0,0,0,0) is #%s" % (ref, r.group(1).decode()))
if not re.search(rb"\x1b\[4[1-7]m", login):
    fails.append("login view buffer has no ESC[41m..47m biome background")
pat = rb"\x1b\[3[0-7]m[" + re.escape("".join(sorted(glyphs)).encode()) + rb"]"
if not re.search(pat, login):
    fails.append("login view buffer has no plant glyph")
if not re.search(rb"\x1b\[1m\x1b\[33m!\x1b\[2\d?m", login):
    fails.append("login view buffer has no yellow bold '!' entity marker at the player tile")

if label == "real" and not fails:
    send("x\n")
    rx = drain(0.5)
    if b"0x0" not in rx:
        fails.append("tell position 'x' replied %r, want 0x0" % rx)
    send("inventory\n")
    rinv = drain(0.8)
    if b"entity/avatar" not in rinv:
        fails.append("inventory did not emit avatar BCP_ITEM frame: %r" % rinv)
    send("k\n")
    rk = drain(0.8)
    if b"north" not in rk:
        fails.append("vim move north 'k' failed: %r" % rk)
    # Check for room and tree BCP item frames
    has_plant = any(p in rk for p in [b"plant/", b"pinus", b"pseudotsuga", b"betula", b"linum", b"abies", b"acacia", b"woodland"])
    if not has_plant:
        fails.append("move north did not receive woodland or plant BCP frames in rk: %r" % rk[:200])
    send("j\n")
    rj = drain(0.8)
    if b"south" not in rj:
        fails.append("vim move south 'j' failed: %r" % rj)

if fails:
    print("[%s] FAIL: %s" % (label, "; ".join(fails)))
    sys.exit(1)
print("[%s] ok: room `%s`(#%s) at 0 0 0 0, backgrounds + plant glyphs + player yellow '!' present, vim moves verified" % (label, name, ref))
PY
	local rc=$?
	kill -TERM "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
	return $rc
}

fail=0
echo "== real seeder must pass"
check_case "$REPO_ROOT/scripts/seed-nd-universe.sh" real || fail=1

for kind in legacy spawn; do
	echo "== $kind mutant must FAIL (proves the test catches the regression)"
	mut="$REPO_ROOT/scripts/.mut-$kind-$$.sh"
	MUT="$MUT $mut"
	make_mutant $kind
	if check_case "$mut" "$kind"; then
		echo "FAIL: $kind mutant passed the assertions; the test has no teeth"
		fail=1
	else
		echo "ok: $kind mutant rejected"
	fi
done

[ $fail -eq 0 ] && echo "PASS: 06-nd-tree-map"
exit $fail
