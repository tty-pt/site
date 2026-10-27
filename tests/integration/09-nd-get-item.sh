#!/bin/bash
# 09-nd-get-item.sh -- Tests picking up items with the `get` command and verifying
# that trees have stick drops in their drop tables.
set -u

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
AXIL_BIN="$REPO_ROOT/external/axil/bin/axil"
[ -x "$AXIL_BIN" ] || AXIL_BIN=axil
TMP="$(mktemp -d /tmp/nd-getitem-XXXXXX)"
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

db="$TMP/u.db"; log="$TMP/axil.log"; port=$((29700 + RANDOM % 90))
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

python3 - "$port" << 'PYEOF'
import base64, re, socket, sys, time, struct
port = int(sys.argv[1])
fails = []

s = socket.create_connection(("127.0.0.1", port))
req = (
    "GET /nd HTTP/1.1\r\n"
    "Host: 127.0.0.1\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Version: 13\r\n\r\n"
)
s.sendall(req.encode())
time.sleep(0.5)

def send(text):
    b, m = text.encode(), b"\x12\x34\x56\x78"
    s.sendall(bytes([0x81, 0x80 | len(b)]) + m + bytes(c ^ m[i % 4] for i, c in enumerate(b)))

def recv(wait=0.5):
    s.settimeout(wait)
    buf = b""
    end = time.time() + wait
    while time.time() < end:
        try:
            d = s.recv(4096)
            if not d: break
            buf += d
        except socket.timeout:
            pass
    return buf

recv(1.0) # drain login
send("look\n")
look_out = recv(0.6).decode('latin1', errors='replace')

# Verify room has stones and broadsword
if "stone" not in look_out.lower() and "broadsword" not in look_out.lower():
    fails.append("initial room missing expected items (stone or broadsword)")

# Test GET command: must NOT output "You go east"
send("get stone\n")
get_out = recv(0.6).decode('latin1', errors='replace')
if "You go east" in get_out:
    fails.append("get stone fell back to movement ('You go east')")

# Check inventory now holds the item
send("inventory\n")
inv_out = recv(0.6).decode('latin1', errors='replace')
if "stone" not in inv_out.lower() and "broadsword" not in inv_out.lower():
    # If stone wasn't got, try getting broadsword
    send("get Steel Broadsword\n")
    recv(0.4)
    send("inventory\n")
    inv_out = recv(0.6).decode('latin1', errors='replace')
    if "broadsword" not in inv_out.lower() and "stone" not in inv_out.lower():
        fails.append("item was not added to inventory after get")

# Test tree stick drops: looking at the tree must reveal sticks
tree_match = re.search(r'([a-zA-Z0-9_ ]*(?:acacia|pine|fir|tree)[a-zA-Z0-9_ ]*)\(#(\d+)\)', look_out, re.I)
if not tree_match:
    fails.append("no tree found in spawn room to test stick drops")
else:
    tree_ref = tree_match.group(2)
    send(f"look #{tree_ref}\n")
    tree_look = recv(0.6).decode('latin1', errors='replace')
    if "stick" not in tree_look.lower():
        fails.append(f"tree #{tree_ref} contents has no sticks: {tree_look[:200]}")
    else:
        # Get a stick from the tree: get #<tree_ref> #<stick_ref>
        stick_match = re.search(r'stick\(#(\d+)\)', tree_look)
        if stick_match:
            stick_ref = stick_match.group(1)
            send(f"get #{tree_ref} #{stick_ref}\n")
            recv(0.4)
            send("inventory\n")
            inv_stick = recv(0.6).decode('latin1', errors='replace')
            if "stick" not in inv_stick.lower():
                fails.append(f"stick #{stick_ref} from tree #{tree_ref} not in inventory after get")

if fails:
    print("FAIL: " + "; ".join(fails))
    sys.exit(1)
print("ok: get command picks up items and trees have stick drops")
PYEOF
rc=$?
[ $rc -eq 0 ] && echo "PASS: 09-nd-get-item"
exit $rc
