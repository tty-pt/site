/**
 * E2E test: gig detail page — add/remove songs
 *
 * Tests:
 *   1. Remove all auto-populated songs → empty state
 *   2. Add a known song via the "Add Song" form
 *   3. Song title + chord data appear on the detail page
 *   4. Remove the song → back to empty state
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const KNOWN_SONG_ID = "a_alegria_esta_no_coracao";
const KNOWN_SONG_TITLE = "A alegria está no coração";

Deno.test({
  name: "gig detail: add and remove songs via API",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let sbId: string | null = null;

  try {
    page.setDefaultNavigationTimeout(15000);
    page.setDefaultTimeout(15000);
    await page.route("**/styles.css", (route) => route.abort());
    const GOTO = { waitUntil: "domcontentloaded" as const };

    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    // ── 0. Create a grp and seed the known song ─────────────────────────
    const grpTitle = `EditRow Grp ${Date.now()}`;
    await page.goto(`${BASE}/grp/add`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', grpTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/, { timeout: 5000 });
    const grpId = page.url().split("/grp/")[1];

    const { token: csrfSeed, cookieHeader: chSeed } = await getCsrfToken(cookieHeader, BASE);
    const seedResp = await fetch(`${BASE}/api/grp/${grpId}/songs`, {
      method: "POST",
      body: new URLSearchParams({ song_id: KNOWN_SONG_ID, format: "any", csrf_token: csrfSeed }).toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chSeed },
      redirect: "manual",
    });
    if (seedResp.status >= 400) throw new Error(`Seed failed: ${seedResp.status}`);
    await seedResp.body?.cancel();

    // ── 1. Create a gig linked to the grp ──────────────────────────
    await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', `SB AddRemove Test ${Date.now()}`);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 5000 });
    sbId = page.url().split("/gig/")[1];

    // ── 2. Navigate to detail page, remove all auto-populated songs ───────
    await page.goto(`${BASE}/gig/${sbId}`, GOTO);
    await page.waitForSelector("body", { timeout: 5000 });
    for (let i = 0; i < 10; i++) {
      const rm = await page.$('[data-testid="remove-song-btn"]');
      if (!rm) break;
      await rm.click();
      await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 8000 });
    }
    await waitForText(page, "body", "No songs yet");

    // ── 3. Add a known song via the list-grade picker ────────────────────
    // Omni mode hides Apply, so submit the search with Enter; then click
    // the whole-row submit button carrying our song's id.
    await page.waitForSelector('form.list-form input[name="q"]', { timeout: 5000 });
    await page.fill('form.list-form input[name="q"]', KNOWN_SONG_TITLE);
    await page.press('form.list-form input[name="q"]', "Enter");
    const pickBtn = `button.hyle-row-action[value="${KNOWN_SONG_ID}"]`;
    await page.waitForSelector(pickBtn, { timeout: 8000 });
    await page.click(pickBtn);
    await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 8000 });
    await waitForText(page, "body", KNOWN_SONG_TITLE);

    // ── 4. Verify chord data renders ──────────────────────────────────────
    await page.waitForSelector('[data-gig-chord-data]', { timeout: 5000 });
    const chordData = await page.textContent('[data-gig-chord-data]');
    if (!chordData || chordData.length < 10)
      throw new Error("Chord data too short or missing");

    // ── 5. Remove the song ────────────────────────────────────────────────
    await page.waitForSelector('[data-testid="remove-song-btn"]', { timeout: 5000 });
    await page.click('[data-testid="remove-song-btn"]');
    await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 8000 });
    await waitForText(page, "body", "No songs yet");

  } finally {
    await browser.close();
  }
});
