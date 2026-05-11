/**
 * E2E test: song ownership enforcement
 *
 * Tests:
 *   1. User A adds a song
 *   2. User B (different user) tries to GET /song/<id>/edit → 403
 *   3. User B tries to POST /song/<id>/edit → 403
 *   4. User B tries to GET /song/<id>/delete → 403
 *   5. User B tries to POST /song/<id>/delete → 403
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("song ownership: user B cannot edit or delete user A's song", async () => {
  const browser = await chromium.launch();
  const pageA = await browser.newPage();
  const pageB = await browser.newPage();

  const ts = Date.now();
  const title = `Song Ownership Test ${ts}`;
  let songId = "";

  try {
    // ── User A: register, login, add song ─────────────────────────────────────
    await createAndLoginUser(pageA, BASE);

    await pageA.goto(`${BASE}/song/add`);
    await pageA.waitForSelector('input[name="title"]', { timeout: 5000 });
    await pageA.fill('input[name="title"]', title);
    await pageA.click('button[type="submit"]');

    await pageA.waitForURL(`${BASE}/song/**`, { timeout: 5000 });
    songId = pageA.url().split("/song/")[1].replace(/\/$/, "");

    // ── User B: register + login ──────────────────────────────────────────────
    await createAndLoginUser(pageB, BASE);
    const cookies = await pageB.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    // ── 2. User B: GET /song/<id>/edit → expect 403 ───────────────────────────
    const editGetResp = await fetch(`${BASE}/song/${songId}/edit`, {
      headers: { Cookie: cookieHeader },
      redirect: "manual",
    });
    await editGetResp.body?.cancel();
    if (editGetResp.status !== 403) {
      throw new Error(`Expected 403 for User B GET edit, got ${editGetResp.status}`);
    }

    // ── 3. User B: POST /song/<id>/edit → expect 403 ──────────────────────────
    const editPostResp = await fetch(`${BASE}/song/${songId}/edit`, {
      method: "POST",
      headers: {
        Cookie: cookieHeader,
        "Content-Type": "multipart/form-data; boundary=----boundary",
      },
      body: "------boundary\r\nContent-Disposition: form-data; name=\"title\"\r\n\r\nhacked\r\n------boundary--\r\n",
      redirect: "manual",
    });
    await editPostResp.body?.cancel();
    if (editPostResp.status !== 403) {
      throw new Error(`Expected 403 for User B POST edit, got ${editPostResp.status}`);
    }

    // ── 4. User B: GET /song/<id>/delete → expect 403 ────────────────────────
    const delGetResp = await fetch(`${BASE}/song/${songId}/delete`, {
      headers: { Cookie: cookieHeader },
      redirect: "manual",
    });
    await delGetResp.body?.cancel();
    if (delGetResp.status !== 403) {
      throw new Error(`Expected 403 for User B GET delete, got ${delGetResp.status}`);
    }

    // ── 5. User B: POST /song/<id>/delete → expect 403 ───────────────────────
    const { token: csrfB, cookieHeader: chB } = await getCsrfToken(cookieHeader, BASE);
    const delFd = new FormData();
    delFd.append("csrf_token", csrfB);
    const delPostResp = await fetch(`${BASE}/song/${songId}/delete`, {
      method: "POST",
      headers: { Cookie: chB },
      body: delFd,
      redirect: "manual",
    });
    await delPostResp.body?.cancel();
    if (delPostResp.status !== 403) {
      throw new Error(`Expected 403 for User B POST delete, got ${delPostResp.status}`);
    }
  } finally {
    await browser.close();
  }
});
