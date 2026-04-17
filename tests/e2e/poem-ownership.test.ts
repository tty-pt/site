/**
 * E2E test: poem ownership enforcement
 *
 * Tests:
 *   1. User A adds a poem
 *   2. User B (different user) tries to GET /poem/<id>/edit → 403
 *   3. User B tries to POST /poem/<id>/edit → 403
 *   4. User B tries to GET /poem/<id>/delete → 403
 *   5. User B tries to POST /poem/<id>/delete → 403
 *
 * Requires: NDC (8080), Fresh (3000) running.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, registerUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("poem ownership: user B cannot edit or delete user A's poem", async () => {
  const browser = await chromium.launch();
  const pageA = await browser.newPage();
  const pageB = await browser.newPage();

  const ts = Date.now();
  const title = `Poem Ownership Test ${ts}`;
  let poemId = "";

  try {
    // ── User A: register, login, add poem ─────────────────────────────────────
    const userA = await createAndLoginUser(pageA, BASE);
    void userA; // only used for side effects

    await pageA.goto(`${BASE}/poem/add`);
    await pageA.waitForSelector('input[name="title"]', { timeout: 5000 });
    await pageA.fill('input[name="title"]', title);
    await pageA.click('button[type="submit"]');

    await pageA.waitForURL(`${BASE}/poem/**`, { timeout: 5000 });
    const url = pageA.url();
    poemId = url.replace(`${BASE}/poem/`, "").replace(/\/$/, "");

    if (!poemId) {
      throw new Error(`Could not extract poem ID from URL: ${url}`);
    }

    // ── User B: register + login ──────────────────────────────────────────────
    await createAndLoginUser(pageB, BASE);

    // Get User B's session cookie for raw fetch calls
    const cookies = await pageB.context().cookies();
    const sessionCookie = cookies.find((c) => c.name === "QSESSION");
    const cookieHeader = sessionCookie
      ? `QSESSION=${sessionCookie.value}`
      : "";

    // ── 2. User B: GET /poem/<id>/edit → expect 403 ───────────────────────────
    const editGetResp = await fetch(`${BASE}/poem/${poemId}/edit`, {
      headers: { Cookie: cookieHeader },
      redirect: "manual",
    });
    await editGetResp.body?.cancel();
    if (editGetResp.status !== 403) {
      throw new Error(
        `Expected 403 for User B GET edit, got ${editGetResp.status}`,
      );
    }

    // ── 3. User B: POST /poem/<id>/edit → expect 403 ──────────────────────────
    const editPostResp = await fetch(`${BASE}/poem/${poemId}/edit`, {
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
      throw new Error(
        `Expected 403 for User B POST edit, got ${editPostResp.status}`,
      );
    }

    // ── 4. User B: GET /poem/<id>/delete → expect 403 ────────────────────────
    const delGetResp = await fetch(`${BASE}/poem/${poemId}/delete`, {
      headers: { Cookie: cookieHeader },
      redirect: "manual",
    });
    await delGetResp.body?.cancel();
    if (delGetResp.status !== 403) {
      throw new Error(
        `Expected 403 for User B GET delete, got ${delGetResp.status}`,
      );
    }

    // ── 5. User B: POST /poem/<id>/delete → expect 403 ───────────────────────
    const delPostResp = await fetch(`${BASE}/poem/${poemId}/delete`, {
      method: "POST",
      headers: {
        Cookie: cookieHeader,
        "Content-Type": "application/x-www-form-urlencoded",
      },
      body: "",
      redirect: "manual",
    });
    await delPostResp.body?.cancel();
    if (delPostResp.status !== 403) {
      throw new Error(
        `Expected 403 for User B POST delete, got ${delPostResp.status}`,
      );
    }
  } finally {
    await browser.close();

    if (poemId) {
      try {
        await Deno.remove(`items/poem/items/${poemId}`, { recursive: true });
      } catch {
        // Already gone — ignore
      }
    }
  }
});
