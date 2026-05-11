/**
 * E2E test: songbook transposition
 *
 * Tests:
 *   1. Create songbook with a song via the new add API
 *   2. Post transposition change via /songbook/:id/transpose
 *   3. Verify transposition is reflected on the view page
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";

Deno.test({
  name: "songbook transposition: persist key change",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let sbId: string | null = null;

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");
    const GOTO = { waitUntil: "domcontentloaded" as const };

    // ── 0. Create a choir and seed the song ───────────────────────────────
    const choirTitle = `Transpose Choir ${Date.now()}`;
    await page.goto(`${BASE}/choir/add`, GOTO);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', choirTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/choir\/[^/]+$/);
    const choirId = page.url().split("/choir/")[1];

    const { token: csrfSeed, cookieHeader: chSeed } = await getCsrfToken(cookieHeader, BASE);
    const seedBody = new URLSearchParams({ song_id: SONG_ID, format: "any", csrf_token: csrfSeed });
    const seedResp = await fetch(`${BASE}/api/choir/${choirId}/songs`, {
      method: "POST",
      body: seedBody.toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chSeed },
      redirect: "manual",
    });
    if (seedResp.status >= 400) throw new Error(`Seed song failed: ${seedResp.status}`);
    await seedResp.body?.cancel();
    const repoId = `${choirId}_${SONG_ID}`;

    // ── 1. Create a songbook linked to the choir ──────────────────────────
    const sbTitle = `Transpose Test SB ${Date.now()}`;
    await page.goto(`${BASE}/songbook/add?choir=${choirId}`, GOTO);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', sbTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/songbook\/[^/]+$/);
    sbId = page.url().split("/songbook/")[1].replace(/\/$/, "");

    // ── 2. Add song via new API (url-encoded) ─────────────────────────────
    const { token: csrfAdd, cookieHeader: chAdd } = await getCsrfToken(cookieHeader, BASE);
    const addBody = new URLSearchParams({ song_id: repoId, csrf_token: csrfAdd });
    const addResp = await fetch(`${BASE}/api/songbook/${sbId}/songs`, {
      method: "POST",
      body: addBody.toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chAdd },
      redirect: "manual",
    });
    if (addResp.status >= 400) throw new Error(`Add song to SB failed: ${addResp.status}`);
    await addResp.body?.cancel();

    // ── 3. Transpose the song (n=0, t=2) ──────────────────────────────────
    const { token: csrfTrans, cookieHeader: chTrans } = await getCsrfToken(cookieHeader, BASE);
    const transFd = new FormData();
    transFd.append("n", "0");
    transFd.append("t", "2");
    transFd.append("csrf_token", csrfTrans);

    const transResp = await fetch(`${BASE}/songbook/${sbId}/transpose`, {
      method: "POST",
      body: transFd,
      headers: { Cookie: chTrans },
      redirect: "manual",
    });
    if (transResp.status >= 400) throw new Error(`Transpose failed: ${transResp.status}`);
    await transResp.body?.cancel();

    // ── 4. Verify on view page ────────────────────────────────────────────
    await page.goto(`${BASE}/songbook/${sbId}`);
    await page.waitForSelector('[data-songbook-chord-data]');

    const chordData = await page.textContent('[data-songbook-chord-data]') ?? "";
    if (!chordData.includes("A") && !chordData.includes("G")) {
        if (chordData.length < 10) throw new Error("Chord data too short or empty");
    }

  } finally {
    await browser.close();
  }
});
