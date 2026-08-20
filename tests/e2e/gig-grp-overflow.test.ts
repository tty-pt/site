/**
 * E2E test: gig add with a grp field >= 128 bytes
 *
 * Reproduces the gig.c:296-298 stack OOB WRITE: grp[128] and
 * grp[grp_len] = '\0' with grp_len from a 200-byte multipart
 * field. The write lands in addressable neighboring stack (silent on
 * glibc — ASAN only catches it at the 128-boundary, see
 * tests/unit/caller_contract_test.c seq3). Assertions here are
 * daemon-liveness + a sane HTTP outcome; the deterministic memory
 * failure is proven in the C harness.
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

Deno.test("gig: grp field >= 128 bytes does not crash the server", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    const { token, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);

    const title = `Grp Overflow ${Date.now()}`;
    const grp = "X".repeat(200);
    const enc = new TextEncoder();
    const body = multipartBody("----boundary", [
      ["title", enc.encode(title)],
      ["grp", enc.encode(grp)],
      ["csrf_token", enc.encode(token)],
    ]);

    const resp = await fetch(`${BASE}/gig/add`, {
      method: "POST",
      headers: {
        Cookie: ch,
        "Content-Type": "multipart/form-data; boundary=----boundary",
      },
      body,
      redirect: "manual",
    });
    await resp.body?.cancel();

    // 303 is the happy path (redirect to /gig/<id>); anything else
    // must at least not have crashed the daemon.
    const health = await fetch(`${BASE}/`);
    if (health.status !== 200) {
      throw new Error(`Server did not respond 200 after grp overflow (${health.status})`);
    }
    await health.body?.cancel();
  } finally {
    await browser.close();
  }
});
