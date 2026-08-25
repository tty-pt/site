/**
 * E2E test: gig replace song (inline flow)
 *
 * Creates a gig with a song, then uses the inline replace feature
 * (triggered by ?replace=N query param) to swap it for a different song.
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const ORIGINAL_SONG_ID = "a_alegria_esta_no_coracao";
const ORIGINAL_SONG_TITLE = "A alegria está no coração";
const REPLACEMENT_SONG_ID = "abba_part_frei_gilson";
const REPLACEMENT_SONG_TITLE = "Abba (part. Frei Gilson)";
const REPLACEMENT_SONG_LABEL = "Abba (part. Frei Gilson)";

Deno.test({
  name: "gig detail: replace a song via inline replace mode",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let sbId: string | null = null;
  let grpId: string | null = null;

  try {
    page.setDefaultNavigationTimeout(15000);
    page.setDefaultTimeout(15000);
    await page.route("**/styles.css", (route) => route.abort());
    const GOTO = { waitUntil: "domcontentloaded" as const };

    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    // ── 0. Create a grp and seed songs into repertoire ─────────────────────
    const grpTitle = `Replace Test Grp ${Date.now()}`;
    await page.goto(`${BASE}/grp/add`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', grpTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/, { timeout: 5000 });
    grpId = page.url().split("/grp/")[1];

    const { token: csrfSeed, cookieHeader: chSeed } = await getCsrfToken(cookieHeader, BASE);
    
    // Seed both songs into repertoire
    for (const songId of [ORIGINAL_SONG_ID, REPLACEMENT_SONG_ID]) {
      const seedBody = new URLSearchParams({ song_id: songId, format: "any", csrf_token: csrfSeed });
      const seedResp = await fetch(`${BASE}/api/grp/${grpId}/songs`, {
        method: "POST",
        body: seedBody.toString(),
        headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chSeed },
        redirect: "manual",
      });
      if (seedResp.status >= 400) throw new Error(`Seed song failed: ${seedResp.status}`);
      await seedResp.body?.cancel();
    }

    // ── 1. Create gig linked to the grp ───────────────────────────────────
    const sbTitle = `Replace Test ${Date.now()}`;
    await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', sbTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 5000 });
    sbId = page.url().split("/gig/")[1];

    // ── 2. Add original song to gig via API ────────────────────────────────
    const { token: csrfAdd, cookieHeader: chAdd } = await getCsrfToken(cookieHeader, BASE);
    const addBody = new URLSearchParams({ song_id: ORIGINAL_SONG_ID, csrf_token: csrfAdd });
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

    // ── 3. Navigate to gig detail page and verify original song is there ───
    await page.goto(`${BASE}/gig/${sbId}`, GOTO);
    await waitForText(page, "body", ORIGINAL_SONG_TITLE);

    // ── 4. Click the replace button (🔄) on the first song row ─────────────
    // The replace button is an anchor link: /gig/:id?replace=0
    await page.waitForSelector('a[href*="replace=0"]', { timeout: 5000 });
    await page.click('a[href*="replace=0"]');

    // ── 5. Verify replace mode: chrome + hidden POST form contract ─────────
    await page.waitForURL(/replace=0/, { timeout: 5000 });
    const urlWithReplace = page.url();
    if (!urlWithReplace.includes("replace=0")) {
      throw new Error(`Expected URL with replace=0, got: ${urlWithReplace}`);
    }

    await page.waitForSelector('text=Replace Song #1', { timeout: 5000 });
    await page.waitForSelector(`text=Replacing:`, { timeout: 5000 });
    await waitForText(page, "body", ORIGINAL_SONG_TITLE);

    // Key/format selects are gone: replacement keeps the row's values
    const nModeSelects = await page.locator(
      '#sb-pick-post select, select[name="transpose"], select[name="format"]',
    ).count();
    if (nModeSelects !== 0) {
      throw new Error("Replace picker should not render transpose/format selects");
    }

    // Hidden post form targets the replace endpoint for row 0
    const postAction = await page.getAttribute("#sb-pick-post", "action");
    if (
      !postAction ||
      postAction !== `/api/gig/${sbId}/song/0/replace`
    ) {
      throw new Error(`Expected #sb-pick-post action /api/gig/:id/song/0/replace, got ${postAction}`);
    }
    const nField = await page.getAttribute('#sb-pick-post input[name="n"]', "value");
    if (nField !== "0") {
      throw new Error(`Expected hidden n=0 in picker post form, got ${nField}`);
    }
    const backField = await page.getAttribute('#sb-pick-post input[name="back"]', "value");
    if (!backField || !backField.startsWith("/gig/")) {
      throw new Error(`Expected hidden back=/gig/... in picker post form, got ${backField}`);
    }

    // ── 6. Search for the replacement song ─────────────────────────
    await page.locator('details.hyle-picker-details summary').click();
    await page.waitForSelector('input[name="pick_q_song_id"]', { timeout: 5000 });
    await page.fill('input[name="pick_q_song_id"]', REPLACEMENT_SONG_LABEL);

    const rows = page.locator('.hyle-picker[data-hyle-picker-key="song_id"] .hyle-picker-rows').first();
    await rows.waitFor({ state: "visible" });

    let text = await rows.innerText();
    while (!text.includes(REPLACEMENT_SONG_LABEL)) {
      await page.waitForTimeout(500);
      text = await rows.innerText();
    }

    // Radios are hidden by CSS; click the wrapping option label instead.
    const opt = page.locator(
      'label.hyle-picker-option:has(input[name="song_id"])',
    );
    await opt.first().waitFor();
    await opt.first().click();

    // ── 7. Click the replacement row button (submits sb-pick-post) ─────────
    const addBtn = await page.$('form[id="sb-pick-post"] button[type="submit"]');
    if (!addBtn) throw new Error("gig picker missing submit button");
    await Promise.all([
      page.waitForNavigation(),
      addBtn.click()
    ]);

    // ── 9. Verify redirect back to gig detail (no replace param) ───────────
    const finalUrl = page.url();
    if (finalUrl.includes("replace=")) {
      throw new Error(`Expected URL without replace param, got: ${finalUrl}`);
    }

    // ── 8. Verify the replacement song appears and original is gone ─────────
    await waitForText(page, "body", "Abba (part. Frei Gilson)");
    const bodyText = await page.textContent("body");
    if (bodyText?.includes(ORIGINAL_SONG_TITLE)) {
      throw new Error("Original song still present after replace");
    }
    // Also verify the song title is present
    if (!bodyText?.includes(REPLACEMENT_SONG_TITLE)) {
      throw new Error("Replacement song title not found in body");
    }

    // ── 11. Verify chord data renders for the new song ──────────────────────
    await page.waitForSelector('[data-gig-chord-data]', { timeout: 5000 });
    const chordData = await page.textContent('[data-gig-chord-data]');
    if (!chordData || chordData.length < 10) {
      throw new Error("Chord data too short or missing for replacement song");
    }

  } finally {
    await browser.close();
  }
});