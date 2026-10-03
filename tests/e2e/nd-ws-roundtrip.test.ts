/**
 * E2E test (ST.md §26 G4): the /nd WebSocket round-trip, automated.
 *
 * §26 (CP-2) verified this by hand — register through the site's own
 * /auth/register, carry the QSESSION cookie on the /nd WS upgrade, then
 * `say pong` — and recorded it as "verified manually, NOT yet captured as an
 * automated test". This is that test. It is the site-integrated half: the
 * engine's own WS round-trip is already covered by
 * `external/axil-nd/test.sh`, but nothing covered axil-nd running *inside the
 * site process*, which is exactly the configuration that produced the S5.4 /
 * S5.5 corruption (ST.md §29). Writing it down also pays off immediately:
 * the unauthenticated case caught a real fail-silent decline (no teardown at
 * all, one lingering fd per probe) that only ever showed up as "the client
 * waits out its own timeout".
 *
 * No browser on purpose: the route is a raw WebSocket whose only browser-side
 * requirement is the session cookie, so this drives the socket directly and
 * costs no Chromium against the e2e memory ceiling.
 *
 * Requires: axil running on :8080 with axil-nd in mods.load, and
 * AUTH_SKIP_CONFIRM=1 (as `make test-e2e` sets).
 */

const BASE = "http://localhost:8080";
const HOST = "127.0.0.1";
const PORT = 8080;

/* Fixed identity on purpose. A unique-per-run user would make every run create
 * a NEW player object in the persisted nd world (var/nd/std.db), so the suite
 * would grow the save file a little every time — the same class of pollution
 * that made var/song.types break the *next* run's picker tests. A returning
 * player is reused by name instead, so a repeat run is idempotent. */
const USER = "e2e_nd_ws";
const PASS = "e2e_nd_ws_pw";

/** axil's WS parser (src/ws.c) refuses unmasked client frames, so every
 * client->server frame carries the mask bit with an all-zero key: the bit is
 * what it checks, and a zero key leaves the payload bytes untouched. This is
 * the same shape external/axil-nd/test.sh sends. */
function clientFrame(text: string): Uint8Array<ArrayBuffer> {
  const payload = new TextEncoder().encode(text);
  const frame = new Uint8Array(2 + 4 + payload.length);
  frame.set([0x81, 0x80 | payload.length], 0); // FIN + text, MASK + len
  // mask key stays zero
  frame.set(payload, 6);
  return frame;
}

/** Deno's Conn.read fills a buffer and returns a byte count (or null at EOF),
 * so every read goes through one scratch buffer and hands back a view. */
async function readSome(
  conn: Deno.Conn,
  scratch: Uint8Array<ArrayBuffer>,
): Promise<Uint8Array<ArrayBuffer> | null> {
  const n = await conn.read(scratch);
  return n === null ? null : scratch.subarray(0, n);
}

/** Read with a deadline. A bare read() blocks until data or EOF, so every
 * wait in this file bounds how long it may block: on expiry the test fails
 * with a message naming the phase, and the finally-block's conn.close()
 * releases the pending read. Without this a silent server hangs the suite
 * instead of failing the test -- which is exactly the failure mode the
 * second case exists to catch, and which a stuck earlier draft of this very
 * file demonstrated by sitting past 60s with nothing to say for itself. */
async function readSomeDeadline(
  conn: Deno.Conn,
  scratch: Uint8Array<ArrayBuffer>,
  ms: number,
  phase: string,
): Promise<Uint8Array<ArrayBuffer> | null> {
  let timer: number | undefined;
  try {
    return await Promise.race([
      readSome(conn, scratch),
      new Promise<null>((_, reject) => {
        timer = setTimeout(() => reject(new Error(
          `nd-ws: no response during "${phase}" within ${ms}ms -- peer silent, failing instead of hanging`,
        )), ms);
      }),
    ]);
  } finally {
    clearTimeout(timer);
  }
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

/** Pull whole frames out of `buf`, keeping any partial tail for the next read.
 * Server->client frames are never masked (RFC 6455 5.1) and this route only
 * ever sends short frames, but the 16- and 64-bit length forms are handled
 * anyway so a long room description cannot desync the stream. */
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

/** BCP frame identifiers (external/axil-nd/index.js). A frame whose first two
 * bytes are "#b" is protocol, iden is the third byte; anything else is game
 * text meant for the terminal. */
const BCP = {
  VIEW_BUFFER: 2,
  AUTH_FAILURE: 5,
  AUTH_SUCCESS: 6,
  OUT: 7,
};

function bcpIden(text: string): number | null {
  return text.length >= 3 && text.startsWith("#b") ? text.charCodeAt(2) : null;
}

/** Register, or fall back to login when the fixed user already exists. Either
 * way axil-auth answers 303 and sets QSESSION (libaxil-auth.c handle_login /
 * handle_register both `axil_redirect`). */
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
      `nd-ws: register and login both failed for ${USER} (status ${resp.status})\n${
        text.slice(0, 200)
      }`,
    );
  }
  await resp.body?.cancel();

  const cookies = resp.headers.getSetCookie();
  const session = cookies.map((c) => c.match(/^\s*QSESSION=([^;]*)/)?.[1]).find((v) => v);
  if (!session) {
    throw new Error(`nd-ws: no QSESSION cookie in ${JSON.stringify(cookies)}`);
  }
  return session;
}

/** Hand-rolled upgrade, because Deno's WebSocket takes no headers and this
 * route authenticates from the cookie axil-auth already set. Returns whatever
 * followed the response head in the same segment: the game starts writing
 * frames immediately, often before this client reads again. */
async function upgrade(conn: Deno.Conn, cookie?: string): Promise<Uint8Array<ArrayBufferLike>> {
  const scratch = new Uint8Array(16384);
  const decoder = new TextDecoder();
  const key = btoa(String.fromCharCode(...crypto.getRandomValues(new Uint8Array(16))));
  const req =
    `GET /nd HTTP/1.1\r\nHost: ${HOST}:${PORT}\r\nUpgrade: websocket\r\n` +
    `Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n` +
    `Sec-WebSocket-Key: ${key}\r\n` +
    (cookie ? `Cookie: QSESSION=${cookie}\r\n` : "") +
    `\r\n`;
  await conn.write(new TextEncoder().encode(req));

  let buf: Uint8Array<ArrayBufferLike> = new Uint8Array(0);
  let head = "";
  const deadline = Date.now() + 30000; // backstop; each read below has its own
  // `head` holds the response head WITHOUT its trailing blank line, so it can
  // never itself contain "\r\n\r\n" -- completeness is this flag, not a
  // substring test on the slice (an earlier draft sliced the head off and
  // then waited forever for the terminator inside what remained).
  let complete = false;
  while (!complete) {
    if (Date.now() > deadline) {
      // Say what actually arrived: "no head" and "no bytes at all" are
      // different failures, and only one of them is this test's business.
      const seen = decoder.decode(buf.subarray(0, 200));
      throw new Error(
        `nd-ws: no handshake response head within 10s (read ${buf.length} byte(s)` +
          `${seen ? `, starting ${JSON.stringify(seen)}` : " -- nothing at all"})`,
      );
    }
    const chunk = await readSomeDeadline(conn, scratch, 10000, "WS handshake head");
    if (chunk === null) throw new Error(`nd-ws: server closed during handshake: ${head}`);
    buf = concat(buf, chunk);
    const s = decoder.decode(buf, { stream: true });
    const end = s.indexOf("\r\n\r\n");
    if (end >= 0) {
      head = s.slice(0, end);
      complete = true;
      buf = buf.slice(end + 4);
    } else {
      head = s;
    }
  }
  if (!head.startsWith("HTTP/1.1 101")) {
    throw new Error(`nd-ws: expected 101 Switching Protocols, got:\n${head}`);
  }
  return buf;
}

Deno.test({
  name: "nd: /nd WS upgrade with a site session resolves the player and round-trips a command",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const cookie = await sessionCookie();
    const conn = await Deno.connect({ hostname: HOST, port: PORT });
    const decoder = new TextDecoder();
    try {
      let buf = await upgrade(conn, cookie);

      await conn.write(clientFrame("say pong\n"));

      // Collect until the echo comes back, or give up. The game answers on the
      // same socket that carries the room view, so protocol frames arrive
      // interleaved and are kept apart from the game text.
      const texts: string[] = [];
      const scratch = new Uint8Array(16384);
      const until = Date.now() + 10000;
      while (Date.now() < until && !texts.some((t) => t.includes("You say: pong"))) {
        const remain = until - Date.now();
        const chunk = await readSomeDeadline(conn, scratch, remain, "say pong echo");
        if (chunk === null) break;
        buf = concat(buf, chunk);
        const { frames, rest } = takeFrames(buf);
        buf = rest;
        for (const f of frames) {
          if (f.opcode === 0x8) throw new Error("nd-ws: server closed the WS (close frame)");
          if (f.opcode !== 0x1 && f.opcode !== 0x2) continue; // ping/pong
          const text = decoder.decode(f.payload);
          if (bcpIden(text) === null) texts.push(text);
        }
      }

      if (!texts.some((t) => t.includes("You say: pong"))) {
        throw new Error(
          `nd-ws: no "You say: pong" round-trip. Server said:\n${
            JSON.stringify(texts.slice(0, 10))
          }`,
        );
      }
    } finally {
      conn.close();
    }
  },
});

Deno.test({
  name: "nd: an unauthenticated /nd upgrade is told AUTH_FAILURE and closed, not left hanging",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
  /* The upgrade itself is expected to succeed at the HTTP level -- ws_init()
   * queues the 101 before any module hook runs -- so the refusal arrives in
   * the protocol stream as a BCP.AUTH_FAILURE frame. What must not happen is
   * the socket being left open afterwards: axil's upgrade path takes no
   * teardown action on a decline, so without axil-nd closing it the client
   * sits on a live, silent connection holding the descriptor until its own
   * timeout. That is the same shape as the S5.4 fd leak, one layer up. */
    const conn = await Deno.connect({ hostname: HOST, port: PORT });
    const decoder = new TextDecoder();
    try {
      let buf = await upgrade(conn);
      const scratch = new Uint8Array(16384);

      // 1. the refusal frame
      let refused = false;
      const until = Date.now() + 10000;
      while (Date.now() < until && !refused) {
        const remain = until - Date.now();
        const chunk = await readSomeDeadline(conn, scratch, remain, "AUTH_FAILURE frame");
        if (chunk === null) break;
        buf = concat(buf, chunk);
        const { frames, rest } = takeFrames(buf);
        buf = rest;
        for (const f of frames) {
          if (f.opcode !== 0x1 && f.opcode !== 0x2) continue;
          const iden = bcpIden(decoder.decode(f.payload));
          if (iden === BCP.AUTH_FAILURE || iden === BCP.AUTH_SUCCESS) refused = true;
        }
      }
      if (!refused) {
        throw new Error(
          "nd-ws: unauthenticated /nd upgrade got no AUTH_FAILURE frame within 10s",
        );
      }

      // 2. and the connection dropped, rather than lingering. Drain whatever
      // the server flushed on the way out (the close frame follows the
      // refusal); only prompt EOF passes, bounded by one shared budget.
      const closed = Date.now() + 5000;
      const stillOpen = () =>
        new Error(
          "nd-ws: unauthenticated /nd connection still open 5s after AUTH_FAILURE -- " +
            "the client would hang on a silent socket",
        );
      for (;;) {
        const remain = closed - Date.now();
        if (remain <= 0) throw stillOpen();
        try {
          if (await readSomeDeadline(conn, scratch, remain, "socket close after AUTH_FAILURE") === null) {
            return; // EOF: closed, as required
          }
        } catch (e) {
          if (e instanceof Error && e.message.includes("no response during")) throw stillOpen();
          throw e;
        }
      }
    } finally {
      conn.close();
    }
  },
});