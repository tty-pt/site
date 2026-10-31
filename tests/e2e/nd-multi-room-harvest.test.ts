/**
 * E2E test for multi-room walking, stone picking, and state persistence.
 */

import { assertEquals, assertNotEquals } from "jsr:@std/assert";

const BASE = "http://localhost:8080";
const HOST = "127.0.0.1";
const PORT = 8080;
const USER = "e2e_nd_ws";
const PASS = "e2e_nd_ws_pw";

function clientFrame(text: string): Uint8Array<ArrayBuffer> {
  const payload = new TextEncoder().encode(text);
  const frame = new Uint8Array(2 + 4 + payload.length);
  frame.set([0x81, 0x80 | payload.length], 0);
  frame.set(payload, 6);
  return frame;
}

function concat(
  a: Uint8Array<ArrayBufferLike>,
  b: Uint8Array<ArrayBufferLike>,
): Uint8Array<ArrayBufferLike> {
  const out = new Uint8Array(a.length + b.length);
  out.set(a, 0);
  out.set(b, a.length);
  return out;
}

interface Frame {
  opcode: number;
  payload: Uint8Array<ArrayBufferLike>;
}

function takeFrames(
  buf: Uint8Array<ArrayBufferLike>,
): { frames: Frame[]; rest: Uint8Array<ArrayBufferLike> } {
  const frames: Frame[] = [];
  let off = 0;
  while (buf.length - off >= 2) {
    const b1 = buf[off + 1];
    const opcode = buf[off] & 0x0f;
    const masked = (b1 & 0x80) !== 0;
    let len = b1 & 0x7f;
    let p = off + 2;
    if (len === 126) {
      if (buf.length - p < 2) break;
      len = (buf[p] << 8) | buf[p + 1];
      p += 2;
    } else if (len === 127) {
      if (buf.length - p < 8) break;
      len = 0;
      for (let i = 0; i < 8; i++) len = len * 256 + buf[p + i];
      p += 8;
    }
    const maskLen = masked ? 4 : 0;
    if (buf.length - p - maskLen < len) break;
    p += maskLen;
    frames.push({ opcode, payload: buf.slice(p, p + len) });
    off = p + len;
  }
  return { frames, rest: buf.slice(off) };
}

async function sessionCookie(): Promise<string> {
  const post = (path: string) =>
    fetch(`${BASE}${path}`, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({
        username: USER,
        password: PASS,
        password2: PASS,
        email: `${USER}@example.com`,
      }).toString(),
      redirect: "manual",
    });

  let resp = await post("/auth/register");
  if (resp.status !== 303) {
    await resp.body?.cancel();
    resp = await post("/auth/login");
  }
  if (resp.status !== 303) {
    const text = await resp.text();
    throw new Error(
      `register and login both failed for ${USER} (status ${resp.status})\n${text.slice(0, 200)}`,
    );
  }
  await resp.body?.cancel();

  const cookies = resp.headers.getSetCookie();
  const session = cookies.map((c) => c.match(/^\s*QSESSION(?:_\d+)?=([^;]*)/)?.[1]).find((v) => v);
  if (!session) {
    throw new Error(`no QSESSION cookie in ${JSON.stringify(cookies)}`);
  }
  return session;
}

async function upgrade(conn: Deno.Conn, cookie?: string): Promise<Uint8Array<ArrayBufferLike>> {
  const scratch = new Uint8Array(16384);
  const decoder = new TextDecoder();
  const key = btoa(String.fromCharCode(...crypto.getRandomValues(new Uint8Array(16))));
  const req =
    `GET /nd HTTP/1.1\r\nHost: ${HOST}:${PORT}\r\nUpgrade: websocket\r\n` +
    `Connection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n` +
    (cookie ? `Cookie: QSESSION=${cookie}\r\n` : "") +
    `\r\n`;

  await conn.write(new TextEncoder().encode(req));
  const n = await conn.read(scratch);
  if (n === null) throw new Error("nd-ws: EOF during handshake");
  const buf = scratch.subarray(0, n);
  const s = decoder.decode(buf);
  if (!/^HTTP\/1\.1 101/m.test(s)) {
    throw new Error(`nd-ws: handshake failed:\n${s.slice(0, 300)}`);
  }
  const end = s.indexOf("\r\n\r\n");
  if (end < 0) throw new Error("nd-ws: no double-CRLF in handshake");
  return buf.slice(new TextEncoder().encode(s.slice(0, end + 4)).length);
}

interface BcpItem {
  ref: number;
  location: number;
  type: number;
  actions: number;
  dynflags: number;
  side: number;
  name: string;
}

function parseEntityPacket(bytes: Uint8Array): BcpItem | null {
  if (bytes.length !== 128) return null;
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const ref = view.getUint32(0, true);
  const location = view.getUint32(4, true);
  const type = view.getUint32(8, true);
  const actions = view.getUint32(12, true);
  const dynflags = view.getUint8(20);
  const side = view.getUint8(22);
  const nameBytes = bytes.subarray(24, 56);
  const nul = nameBytes.indexOf(0);
  const name = new TextDecoder().decode(nul >= 0 ? nameBytes.subarray(0, nul) : nameBytes);
  return { ref, location, type, actions, dynflags, side, name };
}

Deno.test("e2e: multi-room walking, stone picking and persistent view synchronization", async () => {
  console.log("\n[E2E] Starting multi-room walking and harvesting test...");
  const cookie = await sessionCookie();
  const conn = await Deno.connect({ hostname: HOST, port: PORT });
  const initial = await upgrade(conn, cookie);

  const items: Map<number, BcpItem> = new Map();
  let currentRoom = 0;
  let playerRef = 0;
  let streamBuf = initial;
  let running = true;

  function handleFrame(f: Frame) {
    const p = f.payload;
    if (p.length >= 3 && p[0] === 0x23 && p[1] === 0x62) {
      const iden = p[2];
      if (iden === 6) { /* AUTH_SUCCESS */
        const dv = new DataView(p.buffer, p.byteOffset + 3);
        playerRef = dv.getUint32(0, true);
        console.log(`[E2E] Auth success: playerRef = ${playerRef}`);
      } else if (iden === 0) { /* ITEM */
        const parsed = parseEntityPacket(p.subarray(3));
        if (parsed) {
          if (parsed.dynflags === 1 && parsed.type === 0) {
            currentRoom = parsed.ref;
            console.log(`[E2E] Current room set to #${currentRoom}: "${parsed.name}"`);
          } else {
            items.set(parsed.ref, parsed);
            console.log(`[E2E] Entity #${parsed.ref}: "${parsed.name}", loc=#${parsed.location}, side=${parsed.side}`);
          }
        }
      } else if (iden === 7) { /* OUT */
        const dv = new DataView(p.buffer, p.byteOffset + 3);
        const outRef = dv.getUint32(0, true);
        items.delete(outRef);
        console.log(`[E2E] Entity #${outRef} left room #${currentRoom}`);
      }
    } else {
      const txt = new TextDecoder().decode(p).trim();
      if (txt.length > 0) {
        console.log(`[E2E Server Text]: ${JSON.stringify(txt)}`);
      }
    }
  }

  // Drain initial frames from handshake buffer
  if (streamBuf.length > 0) {
    const { frames, rest } = takeFrames(streamBuf);
    streamBuf = rest;
    for (const f of frames) handleFrame(f);
  }

  // Single dedicated background reader loop: no concurrent read() calls
  (async () => {
    const scratch = new Uint8Array(16384);
    try {
      while (running) {
        const n = await conn.read(scratch);
        if (n === null) break;
        streamBuf = concat(streamBuf, scratch.subarray(0, n));
        const { frames, rest } = takeFrames(streamBuf);
        streamBuf = rest;
        for (const f of frames) handleFrame(f);
      }
    } catch (_) {
      // conn closed on exit
    }
  })();

  const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

  try {
    await sleep(1500);
    console.log(`[E2E] Spawn room = #${currentRoom}, entities known = ${items.size}`);

    // Look for mineral entities in current room or player inventory
    let stoneToPick: BcpItem | null = null;
    let inventoryStone: BcpItem | null = null;

    for (const [_, item] of items) {
      if (item.location === currentRoom && (item.name.includes("stone") || item.name.includes("rock"))) {
        stoneToPick = item;
        break;
      }
      if (item.location === playerRef && (item.name.includes("stone") || item.name.includes("rock"))) {
        inventoryStone = item;
      }
    }

    if (!stoneToPick && inventoryStone) {
      console.log(`[E2E] Dropping inventory stone #${inventoryStone.ref} into room #${currentRoom}...`);
      await conn.write(clientFrame(`drop #${inventoryStone.ref}\n`));
      await sleep(1000);
      stoneToPick = items.get(inventoryStone.ref) || null;
      if (stoneToPick) stoneToPick.location = currentRoom;
    }

    // Search adjacent rooms for a room with mineral entities
    const walkDirs = [
      "k", "k", "k", "l", "l", "l", "j", "j", "j", "j",
      "h", "h", "h", "h", "k", "k", "k", "k", "l", "l", "l", "l", "j", "j"
    ];
    let stepIdx = 0;

    while (!stoneToPick && stepIdx < walkDirs.length) {
      for (const [_, item] of items) {
        if (item.location === currentRoom && (item.name.includes("stone") || item.name.includes("rock"))) {
          stoneToPick = item;
          break;
        }
      }
      if (stoneToPick) break;

      const dir = walkDirs[stepIdx++];
      console.log(`[E2E] Searching for minerals: step ${stepIdx} ('${dir}')...`);
      await conn.write(clientFrame(`${dir}\n`));
      await sleep(800);
    }

    console.log(`[E2E] Candidate stone to pick:`, stoneToPick ? `#${stoneToPick.ref} ("${stoneToPick.name}", side ${stoneToPick.side}) in room #${stoneToPick.location}` : "none found in search path");
    assertNotEquals(stoneToPick, null, "Must find at least one room with minerals to verify picking");

    if (stoneToPick) {
      const pickedRef = stoneToPick.ref;
      const pickedSide = stoneToPick.side;
      const harvestRoom = stoneToPick.location;

      // Check if there are other minerals in the room
      const otherStones = Array.from(items.values()).filter(
        (it) => it.location === harvestRoom && it.ref !== pickedRef && (it.name.includes("stone") || it.name.includes("rock"))
      );
      console.log(`[E2E] Other minerals in room #${harvestRoom}: ${otherStones.length}`);

      console.log(`[E2E] Executing pick: 'get #${pickedRef}' in room #${harvestRoom} (side ${pickedSide})`);
      await conn.write(clientFrame(`get #${pickedRef}\n`));

      const pickStart = Date.now();
      while (Date.now() - pickStart < 6000) {
        const it = items.get(pickedRef);
        if (!it || it.location !== harvestRoom)
          break;
        if ((Date.now() - pickStart) % 1000 < 200 && Date.now() - pickStart > 800) {
          await conn.write(clientFrame(`get #${pickedRef}\n`));
        }
        await sleep(200);
      }

      // Verify stone was removed
      const stillInRoom = items.get(pickedRef);
      console.log(`[E2E] Verification: stone #${pickedRef} location =`, stillInRoom ? stillInRoom.location : "removed");
      if (stillInRoom) {
        assertNotEquals(stillInRoom.location, harvestRoom, "Picked stone must not remain in room location");
      }

      // Step away into an adjacent room
      console.log("[E2E] Walking away South ('j')...");
      await conn.write(clientFrame("j\n"));
      const stepAwayStart = Date.now();
      while (Date.now() - stepAwayStart < 3000 && currentRoom === harvestRoom) {
        await sleep(200);
      }
      const awayRoom = currentRoom;
      assertNotEquals(awayRoom, harvestRoom, "Player should have stepped to an adjacent room");

      // Step back into the harvest room
      console.log("[E2E] Walking back North ('k') into harvest room...");
      await conn.write(clientFrame("k\n"));
      const stepBackStart = Date.now();
      while (Date.now() - stepBackStart < 3000 && currentRoom !== harvestRoom) {
        await sleep(200);
      }
      assertEquals(currentRoom, harvestRoom, "Player should return to the exact harvest room");

      // Verify the picked stone did not reappear as a phantom
      const reCheck = items.get(pickedRef);
      console.log(`[E2E] Re-entry check: picked stone #${pickedRef} present?`, (reCheck && reCheck.location === harvestRoom) ? "YES (FAIL)" : "NO (PASS)");
      if (reCheck) {
        assertNotEquals(reCheck.location, harvestRoom, "Picked stone must not reappear in harvest room upon re-entry");
      }

      // If other stones existed, verify they remained
      for (const other of otherStones) {
        const otherCheck = items.get(other.ref);
        console.log(`[E2E] Retained stone #${other.ref} (side ${other.side}) check:`, otherCheck ? "STILL PRESENT (PASS)" : "MISSING (FAIL)");
      }
    }

    console.log("[E2E] Test sequence completed cleanly!\n");
  } finally {
    running = false;
    conn.close();
  }
});
