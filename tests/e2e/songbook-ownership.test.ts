/**
 * E2E test: songbook ownership enforcement
 *
 * Tests:
 *   1. User A adds a songbook
 *   2. User B (different user) tries to GET /songbook/<id>/edit → 403
 *   3. User B tries to POST /songbook/<id>/edit → 403
 *   4. User B tries to GET /songbook/<id>/delete → 403
 *   5. User B tries to POST /songbook/<id>/delete → 403
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("songbook ownership: user B cannot edit or delete user A's songbook", async () => {
  const browser = await chromium.launch();
  const pageA = await browser.newPage();
  const pageB = await browser.newPage();

  const ts = Date.now();
  const title = `Songbook Ownership Test ${ts}`;
  let sbId = "";

  try {
    // ── User A: register, login, add songbook ─────────────────────────────────
    await createAndLoginUser(pageA, BASE);

    await pageA.goto(`${BASE}/songbook/add`);
    await pageA.waitForSelector('input[name="title"]', { timeout: 5000 });
    await pageA.fill('input[name="title"]', title);
    await pageA.click('button[type="submit"]');

    await pageA.waitForURL(`${BASE}/songbook/**`, { timeout: 5000 });
    sbId = pageA.url().split("/songbook/")[1].replace(/\/$/, "");

    // ── User B: register + login ──────────────────────────────────────────────
    await createAndLoginUser(pageB, BASE);
    const cookies = await pageB.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    // ── 2. User B: GET /songbook/<id>/edit → expect 403 ───────────────────────
    const editGetResp = await fetch(`${BASE}/songbook/${sbId}/edit`, {
      headers: { Cookie: cookieHeader },
      redirect: "manual",
    });
    await editGetResp.body?.cancel();
    if (editGetResp.status !== 403) {
      throw new Error(`Expected 403 for User B GET edit, got ${editGetResp.status}`);
    }

    // ── 3. User B: POST /songbook/<id>/edit → expect 403 ──────────────────────
    const editPostResp = await fetch(`${BASE}/songbook/${sbId}/edit`, {
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

    // ── 4. User B: GET /songbook/<id>/delete → expect 403 ────────────────────
    const delGetResp = await fetch(`${BASE}/songbook/${sbId}/delete`, {
      headers: { Cookie: cookieHeader },
      redirect: "manual",
    });
    await delGetResp.body?.cancel();
    if (delGetResp.status !== 403) {
      throw new Error(`Expected 403 for User B GET delete, got ${delGetResp.status}`);
    }

    // ── 5. User B: POST /songbook/<id>/delete → expect 403 ───────────────────
    const { token: csrfB, cookieHeader: chB } = await getCsrfToken(cookieHeader, BASE);
    const delFd = new FormData();
    delFd.append("csrf_token", csrfB);
    const delPostResp = await fetch(`${BASE}/songbook/${sbId}/delete`, {
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
