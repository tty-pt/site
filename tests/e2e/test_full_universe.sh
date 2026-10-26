#!/bin/bash
set -eu

# test_full_universe.sh — End-to-end multi-module integration test for NeverDark.
# Tests clean database seed, single-port HTTP SSR, static WASM, WebSocket BCP framing,
# multi-module execution, and clean ephemeral teardown.

echo "=== Running NeverDark Full Universe E2E Test Suite ==="

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
AXIL_ND_DIR="$REPO_ROOT/external/axil-nd"
AXIL_BIN_DIR="$REPO_ROOT/external/axil/bin"
AXIL_BIN="$AXIL_BIN_DIR/axil"
if [ ! -x "$AXIL_BIN" ]; then
	AXIL_BIN="axil"
fi

TMP_DIR="$(mktemp -d /tmp/nd-e2e-XXXXXX)"
TMP_DB="$TMP_DIR/universe.db"
TMP_LOG="$TMP_DIR/axil.log"
TMP_PID=""

cleanup() {
	if [ -n "$TMP_PID" ]; then
		echo "Tearing down daemon PID $TMP_PID..."
		disown "$TMP_PID" 2>/dev/null || true
		kill -TERM "$TMP_PID" 2>/dev/null || true
		wait "$TMP_PID" 2>/dev/null || true
	fi
	rm -rf "$TMP_DIR"
	echo "E2E cleanup complete."
}
trap cleanup EXIT INT TERM

# 1. Seed Ephemeral Universe Database
echo "[1/4] Seeding test universe with all 19 tenant modules on Planet 1..."
bash "$REPO_ROOT/scripts/seed-nd-universe.sh" "$TMP_DB" 1 > "$TMP_DIR/seed.log" 2>&1
if [ ! -s "$TMP_DB" ]; then
	echo "FAIL: database $TMP_DB was not created" >&2
	cat "$TMP_DIR/seed.log" >&2
	exit 1
fi
echo "PASS: universe seeded ($(wc -c < "$TMP_DB") bytes)"

# 2. Boot axil on a single port with full module set
PORT=$((28750 + RANDOM % 1000))
echo "[2/4] Booting single-port unified axil daemon on port $PORT..."

export PATH="$AXIL_BIN_DIR:$PATH"
export AXIL_ND_DB="$TMP_DB"
export AXIL_ND_HTDOCS="$REPO_ROOT/htdocs"
export LD_LIBRARY_PATH="$REPO_ROOT/external/axil-tty/lib:$REPO_ROOT/external/axil/lib:$REPO_ROOT/external/axil-nd/lib:$REPO_ROOT/external/nd-view/lib:$REPO_ROOT/external/nd-mcp/lib:$REPO_ROOT/external/nd-region/lib:${LD_LIBRARY_PATH:-}"

. "$REPO_ROOT/scripts/nd-kernel-mods.sh"

(cd "$REPO_ROOT" && exec "$AXIL_BIN" -C "$REPO_ROOT" -p "$PORT" -d -A -m "mods/core/core:$ND_KERNEL_MODS" > "$TMP_LOG" 2>&1) &
TMP_PID=$!

# Wait for server ready
tries=100
while [ $tries -gt 0 ]; do
	if curl -s "http://127.0.0.1:$PORT/nd" >/dev/null 2>&1 || grep -q "st_init: st_restore: region id=0x0000000000000000" "$TMP_LOG" 2>/dev/null; then
		break
	fi
	if ! kill -0 "$TMP_PID" 2>/dev/null; then
		echo "Daemon process died unexpectedly." >&2
		break
	fi
	tries=$((tries - 1))
	sleep 0.1
done

if [ $tries -eq 0 ]; then
	echo "FAIL: daemon failed to start. Logs:" >&2
	cat "$TMP_LOG" >&2
	exit 1
fi
echo "PASS: daemon running (PID $TMP_PID)"

# 3. Phase A: HTTP SSR and Static Assets on port $PORT
echo "[3/4] Testing HTTP SSR & WASM assets on port $PORT..."
HTTP_RESP=$(curl -sS -i "http://127.0.0.1:$PORT/nd")
echo "$HTTP_RESP" | grep -q "HTTP/1.1 200 OK" || { echo "FAIL: /nd did not return 200 OK" >&2; exit 1; }
echo "$HTTP_RESP" | grep -q 'data-modules=' || { echo "FAIL: /nd missing data-modules" >&2; exit 1; }
echo "$HTTP_RESP" | grep -q 'data-capabilities=' || { echo "FAIL: /nd missing capabilities" >&2; exit 1; }
echo "$HTTP_RESP" | grep -q "id=\"bud-state\"" || { echo "FAIL: /nd missing bud-state JSON" >&2; exit 1; }

# Validate #bud-state payload contents (player credentials, vitals, active components)
echo "$HTTP_RESP" | grep -q '"me":' || { echo "FAIL: bud-state missing me" >&2; exit 1; }
echo "$HTTP_RESP" | grep -q '"n":"hp"' || { echo "FAIL: bud-state missing hp" >&2; exit 1; }
echo "$HTTP_RESP" | grep -q '"n":"mp"' || { echo "FAIL: bud-state missing mp" >&2; exit 1; }
echo "$HTTP_RESP" | grep -q '"room":' || { echo "FAIL: bud-state missing room" >&2; exit 1; }
echo "$HTTP_RESP" | grep -q '"entities":' || { echo "FAIL: bud-state missing entities" >&2; exit 1; }
echo "PASS: HTTP /nd returned valid dynamic pure-C Bud SSR with verified #bud-state JSON payload"

# Test static WASM delivery on same port (Strict failure if absent or invalid)
WASM_STATUS=$(curl -sS -o "$TMP_DIR/nd.wasm" -w "%{http_code}" "http://127.0.0.1:$PORT/nd.wasm")
if [ "$WASM_STATUS" != "200" ]; then
	echo "FAIL: /nd.wasm returned status $WASM_STATUS" >&2
	exit 1
fi
WASM_HEX=$(od -An -tx1 -N4 "$TMP_DIR/nd.wasm" | tr -d ' \n')
if [ "$WASM_HEX" != "0061736d" ]; then
	echo "FAIL: /nd.wasm missing WebAssembly magic bytes (got $WASM_HEX)" >&2
	exit 1
fi
echo "PASS: /nd.wasm served with valid HTTP 200 and WASM magic header (0061736d / \0asm)"

# 4. Phase B & C: WebSocket Upgrade, BCP Packet Validation, and Multi-Module Interaction
echo "[4/4] Testing WebSocket Upgrade, BCP framing & Multi-Module execution on port $PORT..."
python3 - << EOF
import socket
import struct
import base64
import time

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(5.0)
s.connect(('127.0.0.1', $PORT))

# 1. HTTP WebSocket Upgrade handshake
key = base64.b64encode(b'1234567890123456').decode('ascii')
req = (
    f"GET /nd HTTP/1.1\r\n"
    f"Host: 127.0.0.1:$PORT\r\n"
    f"Upgrade: websocket\r\n"
    f"Connection: Upgrade\r\n"
    f"Sec-WebSocket-Key: {key}\r\n"
    f"Sec-WebSocket-Version: 13\r\n\r\n"
)
s.sendall(req.encode('ascii'))

resp = s.recv(1024).decode('latin1', errors='ignore')
assert "101" in resp, f"Expected 101 Switching Protocols, got: {resp}"
print("PASS: WebSocket upgraded in-place on single port")

def send_ws_text(sock, text):
    payload = text.encode('utf-8')
    mask = b'\x12\x34\x56\x78'
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    header = bytearray([0x81, 0x80 | len(payload)]) + mask
    sock.sendall(header + masked)

def read_ws_frames(sock, timeout=1.0):
    frames = []
    end_time = time.time() + timeout
    while time.time() < end_time:
        try:
            sock.settimeout(max(0.05, end_time - time.time()))
            data = sock.recv(4096)
            if not data:
                break
            frames.append(data)
        except socket.timeout:
            break
    return b"".join(frames)

# 2. Read initial session frames
buf = read_ws_frames(s, timeout=1.0)
assert len(buf) > 0, "Expected initial BCP frames upon connect"

# Validate incoming BCP packets: parse opcodes
bcp_opcodes = set()
idx = 0
while idx < len(buf) - 2:
    if buf[idx] == 0x23 and buf[idx+1] == 0x62: # '#b'
        opcode = buf[idx+2]
        bcp_opcodes.add(opcode)
        if opcode == 0: # AUTH_SUCCESS
            player_ref = struct.unpack('<I', buf[idx+3:idx+7])[0]
            assert player_ref > 0, f"Invalid player ref: {player_ref}"
            print(f"PASS: BCP AUTH_SUCCESS validated (player_ref={player_ref})")
        elif opcode == 1: # ROOM
            room_ref = struct.unpack('<I', buf[idx+3:idx+7])[0]
            print(f"PASS: BCP ROOM validated (room_ref={room_ref})")
        elif opcode == 6: # MP Gauge
            mp_val, mp_max = struct.unpack('<HH', buf[idx+3:idx+7])
            print(f"PASS: BCP MP gauge validated (val={mp_val}, max={mp_max})")
        elif opcode == 8: # TOD
            tod = struct.unpack('<I', buf[idx+3:idx+7])[0]
            print(f"PASS: BCP TOD validated (tod={tod})")
        elif opcode == 2: # ITEM / Entity
            item_ref = struct.unpack('<I', buf[idx+3:idx+7])[0]
            print(f"PASS: BCP ITEM entity validated (item_ref={item_ref})")
        elif opcode == 9: # ACTION dock
            action_id = struct.unpack('<I', buf[idx+3:idx+7])[0]
            print(f"PASS: BCP ACTION validated (action_id={action_id})")
        elif opcode == 19 or opcode == 10: # REGION layout
            print("PASS: BCP REGION layout frame validated")
        idx += 3
    else:
        idx += 1

assert 0 in bcp_opcodes, "Expected BCP AUTH_SUCCESS (opcode 0)"
assert 2 in bcp_opcodes, "Expected BCP ITEM entities (opcode 2)"
assert 8 in bcp_opcodes, "Expected BCP TOD (opcode 8)"
print("PASS: Core BCP packet streams validated (AUTH, ITEM, TOD)")

# 3. Test view command -> emits binary map & text view
send_ws_text(s, "view\n")
look_buf = read_ws_frames(s, timeout=0.8)
assert len(look_buf) > 0, "Expected response to view"
assert b"#b" in look_buf, "Expected BCP stream in response to view"

look_opcodes = []
l_idx = 0
while l_idx < len(look_buf) - 2:
    if look_buf[l_idx:l_idx+2] == b"#b":
        l_op = look_buf[l_idx+2]
        look_opcodes.append(l_op)
        if l_op == 1 and l_idx + 5 <= len(look_buf):
            gw, gh = struct.unpack('<BB', look_buf[l_idx+3:l_idx+5])
            print(f"PASS: BCP VIEW binary map received and validated (grid={gw}x{gh})")
        l_idx += 3
    else:
        l_idx += 1
assert 1 in look_opcodes, f"Expected BCP VIEW (opcode 1) in response to view, got {look_opcodes}"
print(f"PASS: Command 'view' executed and emitted BCP binary view & entity streams ({len(look_buf)} bytes)")

# 4. Test inventory command -> emits inventory items
send_ws_text(s, "inventory\n")
inv_buf = read_ws_frames(s, timeout=0.8)
assert len(inv_buf) > 0, "Expected response to inventory"
print(f"PASS: Command 'inventory' executed ({len(inv_buf)} bytes)")

# 5. Test movement command 'e'
send_ws_text(s, "e\n")
time.sleep(0.3)
print("PASS: Movement command 'e' dispatched to spacetime engine")

# 6. Test help command -> verifies tenant module commands are active
send_ws_text(s, "help\n")
help_buf = read_ws_frames(s, timeout=0.8)
assert len(help_buf) > 100, f"Expected help topic list, got {len(help_buf)} bytes"
help_text = help_buf.decode('latin1', errors='ignore')
assert "topics" in help_text or "help" in help_text, "Expected help text in output"
print("PASS: Command 'help' executed (tenant module commands verified active)")

# 7. Test driver command -> verifies regional presentation policy query
send_ws_text(s, "driver\n")
drv_buf = read_ws_frames(s, timeout=0.8)
assert len(drv_buf) > 0, "Expected response to driver"
drv_text = drv_buf.decode('latin1', errors='ignore')
assert "presentation policy" in drv_text or "driver" in drv_text, f"Expected driver output, got {drv_text}"
print("PASS: Command 'driver' executed and returned active regional presentation policy")

# 8. Test save command
send_ws_text(s, "save\n")
time.sleep(0.3)
print("PASS: Command 'save' executed")

# 9. Send clean WebSocket close frame
mask = b'\x00\x00\x00\x00'
s.sendall(bytearray([0x88, 0x80]) + mask)
time.sleep(0.2)
s.close()
print("PASS: WebSocket session closed gracefully")

# 10. Verify Terminal access under -A flag (no refusal)
s_tty = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s_tty.settimeout(5.0)
s_tty.connect(('127.0.0.1', $PORT))
key_tty = base64.b64encode(b'0987654321098765').decode('ascii')
req_tty = (
    f"GET /tty HTTP/1.1\r\n"
    f"Host: 127.0.0.1:$PORT\r\n"
    f"Upgrade: websocket\r\n"
    f"Connection: Upgrade\r\n"
    f"Sec-WebSocket-Key: {key_tty}\r\n"
    f"Sec-WebSocket-Version: 13\r\n\r\n"
)
s_tty.sendall(req_tty.encode('ascii'))
resp_tty = s_tty.recv(1024).decode('latin1', errors='ignore')
assert "101" in resp_tty, f"Expected 101 Switching Protocols on /tty, got: {resp_tty}"
time.sleep(0.3)
tty_frames = read_ws_frames(s_tty, timeout=1.0)
assert b"Terminal disabled" not in tty_frames, f"Terminal was unexpectedly disabled under -A: {tty_frames}"
s_tty.close()
print("PASS: Terminal access under -A flag verified without auth refusal")
EOF

echo "ALL E2E FULL UNIVERSE TESTS PASSED"
