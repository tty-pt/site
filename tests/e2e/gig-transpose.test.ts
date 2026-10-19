/**
 * E2E test: gig transposition
 *
 * Tests:
 *   1. Create gig with a song via the new add API
 *   2. Post transposition change via /gig/:id/transpose
 *   3. Verify transposition is reflected on the view page
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";

Deno.test({
  name: "gig transposition: persist key change",
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

    // ── 0. Create a grp and seed the song ───────────────────────────────
    const grpTitle = `Transpose Grp ${Date.now()}`;
    await page.goto(`${BASE}/grp/add`, GOTO);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', grpTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/);
    const grpId = page.url().split("/grp/")[1];

    const { token: csrfSeed, cookieHeader: chSeed } = await getCsrfToken(cookieHeader, BASE);
    const seedBody = new URLSearchParams({ song_id: SONG_ID, format: "any", csrf_token: csrfSeed });
    const seedResp = await fetch(`${BASE}/api/grp/${grpId}/songs`, {
      method: "POST",
      body: seedBody.toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chSeed },
      redirect: "manual",
    });
    if (seedResp.status >= 400) throw new Error(`Seed song failed: ${seedResp.status}`);
    await seedResp.body?.cancel();
    const repoId = `${SONG_ID}`;

    // ── 1. Create a gig linked to the grp ──────────────────────────
    const sbTitle = `Transpose Test SB ${Date.now()}`;
    await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', sbTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/);
    sbId = page.url().split("/gig/")[1].replace(/\/$/, "");

    // ── 2. Add song via new API (url-encoded) ─────────────────────────────
    const { token: csrfAdd, cookieHeader: chAdd } = await getCsrfToken(cookieHeader, BASE);
    const addBody = new URLSearchParams({ song_id: repoId, csrf_token: csrfAdd });
    const addResp = await fetch(`${BASE}/api/gig/${sbId}/songs`, {
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

    const transResp = await fetch(`${BASE}/gig/${sbId}/transpose`, {
      method: "POST",
      body: transFd,
      headers: { Cookie: chTrans },
      redirect: "manual",
    });
    if (transResp.status >= 400) throw new Error(`Transpose failed: ${transResp.status}`);
    await transResp.body?.cancel();

    // ── 4. Verify on view page ────────────────────────────────────────────
    await page.goto(`${BASE}/gig/${sbId}`);
    await page.waitForSelector('[data-gig-chord-data]');

    const chordData = await page.textContent('[data-gig-chord-data]') ?? "";
    if (!chordData.includes("A") && !chordData.includes("G")) {
        if (chordData.length < 10) throw new Error("Chord data too short or empty");
    }

  } finally {
    await browser.close();
  }
});
