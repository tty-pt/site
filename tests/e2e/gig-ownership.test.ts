/**
 * E2E test: gig ownership enforcement
 *
 * Tests:
 *   1. User A adds a gig
 *   2. User B (different user) tries to GET /gig/<id>/edit → 403
 *   3. User B tries to POST /gig/<id>/edit → 403
 *   4. User B tries to GET /gig/<id>/delete → 403
 *   5. User B tries to POST /gig/<id>/delete → 403
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("gig ownership: user B cannot edit or delete user A's gig", async () => {
  const browser = await chromium.launch();
  const contextA = await browser.newContext();
  const pageA = await contextA.newPage();
  const contextB = await browser.newContext();
  const pageB = await contextB.newPage();

  const ts = Date.now();
  const title = `Gig Ownership Test ${ts}`;
  let sbId = "";

  try {
    // ── User A: register, login, add gig ─────────────────────────────────
    await createAndLoginUser(pageA, BASE);

    await pageA.goto(`${BASE}/gig/add`);
    await pageA.waitForSelector('input[name="title"]', { timeout: 5000 });
    await pageA.fill('input[name="title"]', title);
    await Promise.all([
      pageA.waitForURL(/\/gig\/(?!add$)[^\/]+$/, { timeout: 10000 }),
      pageA.click('form[method="POST"] button[type="submit"]'),
    ]);
    sbId = pageA.url().split("/gig/")[1].replace(/\/$/, "");

    // ── User B: register + login ──────────────────────────────────────────────
    await createAndLoginUser(pageB, BASE);
    const cookies = await contextB.cookies();
    const sessionCookie = cookies.find((c) => c.name === "QSESSION");
    const cookieHeader = sessionCookie ? `QSESSION=${sessionCookie.value}` : "";

    // ── 2. User B: GET /gig/<id>/edit → expect 403 ───────────────────────
    const editGetResp = await fetch(`${BASE}/gig/${sbId}/edit`, {
      headers: { Cookie: cookieHeader },
      redirect: "manual",
    });
    await editGetResp.body?.cancel();
    if (editGetResp.status !== 403) {
      throw new Error(`Expected 403 for User B GET edit, got ${editGetResp.status}`);
    }

    // ── 3. User B: POST /gig/<id>/edit → expect 403 ──────────────────────
    const editPostResp = await fetch(`${BASE}/gig/${sbId}/edit`, {
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

    // ── 4. User B: GET /gig/<id>/delete → expect 403 ────────────────────
    const delGetResp = await fetch(`${BASE}/gig/${sbId}/delete`, {
      headers: { Cookie: cookieHeader },
      redirect: "manual",
    });
    await delGetResp.body?.cancel();
    if (delGetResp.status !== 403) {
      throw new Error(`Expected 403 for User B GET delete, got ${delGetResp.status}`);
    }

    // ── 5. User B: POST /gig/<id>/delete → expect 403 ───────────────────
    const { token: csrfB, cookieHeader: chB } = await getCsrfToken(cookieHeader, BASE);
    const delFd = new FormData();
    delFd.append("csrf_token", csrfB);
    const delPostResp = await fetch(`${BASE}/gig/${sbId}/delete`, {
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
