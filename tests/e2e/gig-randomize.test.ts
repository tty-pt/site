/**
 * E2E test: gig randomize
 *
 * Creates a gig, seeds it with one song entry, then clicks the
 * randomize button on the detail page and verifies the page stays on
 * the gig detail (the randomize POST redirects back to the same page).
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SEED_SONG_ID = "abba_part_frei_gilson";

Deno.test("gig randomize: create gig → seed data → click randomize → stays on page", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  let sbId: string | null = null;

  try {
    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");
    const GOTO = { waitUntil: "domcontentloaded" as const };

    // ── 0. Create a grp and seed the known song into repertoire ─────────
    const grpTitle = `Randomize Grp ${Date.now()}`;
    await page.goto(`${BASE}/grp/add`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', grpTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/, { timeout: 5000 });
    const grpId = page.url().split("/grp/")[1];

    const { token: csrfSeed, cookieHeader: chSeed } = await getCsrfToken(cookieHeader, BASE);
    const seedBody = new URLSearchParams({ song_id: SEED_SONG_ID, format: "any", csrf_token: csrfSeed });
    const seedResp = await fetch(`${BASE}/api/grp/${grpId}/songs`, {
      method: "POST",
      body: seedBody.toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chSeed },
      redirect: "manual",
    });
    if (seedResp.status >= 400) throw new Error(`Seed song failed: ${seedResp.status}`);
    await seedResp.body?.cancel();
    const repoId = `${SEED_SONG_ID}`;

    // ── 1. Create gig linked to the grp ────────────────────────────
    const sbTitle = `Randomize Test ${Date.now()}`;
    await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', sbTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 5000 });

    const sbUrl = page.url();
    sbId = sbUrl.split("/gig/")[1];
    if (!sbId) throw new Error(`Could not extract gig id from ${sbUrl}`);

    // ── 2. Add song to gig via API ───────────────────────────────────
    const { token: csrfAdd, cookieHeader: chAdd } = await getCsrfToken(cookieHeader, BASE);
    const addBody = new URLSearchParams({ song_id: repoId, csrf_token: csrfAdd });
    const addResp = await fetch(`${BASE}/api/gig/${sbId}/songs`, {
      method: "POST",
      body: addBody.toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chAdd },
      redirect: "manual",
    });
    if (addResp.status >= 400) {
      const text = await addResp.text();
      throw new Error(`Add song via API failed: HTTP ${addResp.status}: ${text.slice(0, 200)}`);
    }
    await addResp.body?.cancel();

    // ── 3. Navigate to gig detail page ───────────────────────────────
    await page.goto(`${BASE}/gig/${sbId}`);
    await page.waitForSelector('form[action$="/randomize"] button[type="submit"]', { timeout: 8000 });

    // ── 4. Click the randomize button ─────────────────────────────────────
    const [randomizeRes] = await Promise.all([
      page.waitForResponse((r) => r.url().includes("/randomize")),
      page.click('form[action$="/randomize"] button[type="submit"]'),
    ]);
    if (randomizeRes.status() >= 400) {
      const body = await randomizeRes.text();
      throw new Error(`Randomize returned HTTP ${randomizeRes.status()}: ${body}`);
    }

    // ── 5. Verify the page still renders the gig ─────────────────────
    await page.waitForSelector("h1", { timeout: 5000 });
    const h1Text = await page.textContent("h1");
    if (!h1Text?.includes(sbTitle)) {
      throw new Error(
        `After randomize, <h1> does not contain gig title "${sbTitle}", got: "${h1Text}"`,
      );
    }
  } finally {
    await browser.close();
  }
});
