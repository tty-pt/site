/**
 * E2E test: song add with invalid UTF-8 bytes in the title
 *
 * Sends a raw multipart body whose title field contains a lone 0xFF
 * continuation byte (invalid UTF-8). This exercises the iconv
 * EILSEQ/EINVAL paths in axil_slugify and the mpfd_get truncated-copy
 * contract with non-text bytes. Requires a CSRF token + cookies.
 *
 * Expected today (glibc): either 303 (redirect, EILSEQ skipped) or a
 * 4xx/5xx response — the important assertion is that the server daemon
 * stays alive afterwards (returns 200 on a follow-up GET).
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

function multipartBody(
  boundary: string,
  fields: Array<[string, Uint8Array]>,
): Uint8Array {
  const parts: Uint8Array[] = [];
  const enc = new TextEncoder();
  for (const [name, value] of fields) {
    parts.push(enc.encode(
      `--${boundary}\r\nContent-Disposition: form-data; name="${name}"\r\n\r\n`,
    ));
    parts.push(value);
    parts.push(enc.encode("\r\n"));
  }
  parts.push(enc.encode(`--${boundary}--\r\n`));
  const total = parts.reduce((n, p) => n + p.length, 0);
  const out = new Uint8Array(total);
  let off = 0;
  for (const p of parts) {
    out.set(p, off);
    off += p.length;
  }
  return out;
}

Deno.test("song: invalid UTF-8 title does not crash the server", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    const { token, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);

    // title = 8 bytes: "A" + 0xFF + "bc" + 0xC0 + "ef" + 0xFF  (invalid UTF-8)
    const title = new Uint8Array([0x41, 0xff, 0x62, 0x63, 0xc0, 0x65, 0x66, 0xff]);
    const body = multipartBody("----boundary", [
      ["title", title],
      ["csrf_token", new TextEncoder().encode(token)],
    ]);

    const resp = await fetch(`${BASE}/song/add`, {
      method: "POST",
      headers: {
        Cookie: ch,
        "Content-Type": "multipart/form-data; boundary=----boundary",
      },
      body,
      redirect: "manual",
    });
    await resp.body?.cancel();

    const health = await fetch(`${BASE}/`);
    if (health.status !== 200) {
      throw new Error(`Server did not respond 200 after invalid-UTF-8 add (${health.status})`);
    }
    await health.body?.cancel();
  } finally {
    await browser.close();
  }
});
