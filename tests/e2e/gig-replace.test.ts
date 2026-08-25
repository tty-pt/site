/**
 * E2E test: gig replace song (title-as-trigger dropdown flow)
 *
 * In the gig detail (songbook), when you are the author, each song title
 * acts as an omni-dropdown trigger. Clicking the title expands an inline
 * picker right in place; picking a song and clicking Replace swaps that row.
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
  name: "gig detail: replace a song via song title dropdown trigger",
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

    // ── 4. Verify the old replace link (🔁 / ?replace=0) is REMOVED ───────
    const legacyReplaceLinks = await page.locator('a[href*="replace="]').count();
    if (legacyReplaceLinks !== 0) {
      throw new Error(`Expected 0 legacy replace links, found: ${legacyReplaceLinks}`);
    }

    // ── 5. Verify song title acts as the omni-dropdown trigger ─────────────
    const titleSummary = page.locator('#sb-pick-post-0 details.hyle-picker-details summary').first();
    await titleSummary.waitFor({ state: "visible" });
    const titleText = await titleSummary.innerText();
    if (!titleText.includes(ORIGINAL_SONG_TITLE)) {
      throw new Error(`Expected title summary to contain "${ORIGINAL_SONG_TITLE}", got: "${titleText}"`);
    }

    // Verify row 0 POST form targets replace endpoint
    const postAction = await page.getAttribute("#sb-pick-post-0", "action");
    if (!postAction || postAction !== `/api/gig/${sbId}/song/0/replace`) {
      throw new Error(`Expected #sb-pick-post-0 action /api/gig/${sbId}/song/0/replace, got ${postAction}`);
    }

    // Verify sibling GET form exists for row 0
    const siblingForm = await page.$('#pickq-song_id__0');
    if (!siblingForm) {
      throw new Error("Missing sibling GET form #pickq-song_id__0");
    }

    // ── 6. Click title trigger to expand the row's dropdown ────────────────
    await titleSummary.click();

    // Verify search input is scoped to row 0
    const searchInput = page.locator('input[name="pick_q_song_id__0"]');
    await searchInput.waitFor({ state: "visible", timeout: 5000 });

    // Verify clicking outside closes the picker
    const rowDetails = page.locator('#sb-pick-post-0 details.hyle-picker-details');
    await page.locator('h1').click();
    await rowDetails.waitFor({ state: "attached" });
    const isOpen = await rowDetails.getAttribute("open");
    if (isOpen !== null) {
      throw new Error("Expected details to close on click outside");
    }

    // Re-open picker by clicking summary again
    await titleSummary.click();
    await searchInput.waitFor({ state: "visible", timeout: 5000 });

    // ── 7. Search for the replacement song ─────────────────────────────────
    await searchInput.fill(REPLACEMENT_SONG_LABEL);

    const rows = page.locator('#sb-pick-post-0 .hyle-picker-rows').first();
    await rows.waitFor({ state: "visible" });

    let text = await rows.innerText();
    while (!text.includes(REPLACEMENT_SONG_LABEL)) {
      await page.waitForTimeout(500);
      text = await rows.innerText();
    }

    // Verify Replace button is hidden when client scripts are active
    const replaceBtn = page.locator('#sb-pick-post-0 button.gig-song-picker-submit');
    const isBtnVisible = await replaceBtn.isVisible();
    if (isBtnVisible) {
      throw new Error("Expected Replace button to be hidden with client scripts active");
    }

    // ── 8. Select replacement song option (auto-submits on change in JS mode without page reload) ───
    const opt = page.locator(
      `#sb-pick-post-0 label.hyle-picker-option:has(input[name="song_id"][value="${REPLACEMENT_SONG_ID}"])`,
    );
    await opt.first().waitFor();

    let navigated = false;
    page.on("framenavigated", (frame) => {
      if (frame === page.mainFrame()) {
        console.log("NAVIGATED TO:", frame.url());
        navigated = true;
      }
    });

    await opt.first().click();

    // ── 10. Verify NO full page navigation occurred in JS mode ────────────
    await page.waitForTimeout(600);
    if (navigated) {
      throw new Error("Expected in-place replacement without full page reload/navigation");
    }

    // ── 11. Verify the replacement song appears and original is gone ───────
    await waitForText(page, "body", REPLACEMENT_SONG_TITLE);
    const bodyText = await page.textContent("body");
    if (bodyText?.includes(ORIGINAL_SONG_TITLE)) {
      throw new Error("Original song still present after replace");
    }
    if (!bodyText?.includes(REPLACEMENT_SONG_TITLE)) {
      throw new Error("Replacement song title not found in body");
    }

    // ── 12. Verify chord data renders for the new song ─────────────────────
    await page.waitForSelector('[data-gig-chord-data]', { timeout: 5000 });
    const chordData = await page.textContent('[data-gig-chord-data]');
    if (!chordData || chordData.length < 10) {
      throw new Error("Chord data too short or missing for replacement song");
    }

    // ── 13. Test no-JS SSR query scoping (?pick_q_song_id__0=...) ─────────
    const noJsResp = await fetch(`${BASE}/gig/${sbId}?pick_q_song_id__0=alegria`, {
      headers: { Cookie: cookieHeader },
    });
    if (noJsResp.status !== 200) {
      throw new Error(`No-JS query returned HTTP ${noJsResp.status}`);
    }
    const noJsHtml = await noJsResp.text();
    if (!noJsHtml.includes('id="pickq-song_id__0"') || !noJsHtml.includes('id="sb-pick-post-0"')) {
      throw new Error("No-JS SSR HTML missing row 0 picker forms");
    }

  } finally {
    await browser.close();
  }
});
