/**
 * E2E regression test: gig detail — removing a specific song row
 *
 * Bug: clicking the 🗑 delete button on song row N always removed
 * row 0 (the first row) because the remove handler read query param
 * `n` while the detail-page form posts to /api/gig/:id/song/:n/remove
 * with no query string.
 *
 * Test:
 *   1. Create grp + gig, empty auto-populated songs
 *   2. Add three songs A, B, C via the API
 *   3. Click remove on the SECOND row (B)
 *   4. Verify A and C remain, B is gone
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_A_ID = "a_alegria_esta_no_coracao";
const SONG_A_TITLE = "A alegria está no coração";
const SONG_B_ID = "abba_part_frei_gilson";
const SONG_B_TEXT = "Abba (part. Frei Gilson)";
const SONG_C_ID = "a_bondade_do_senhor";
const SONG_C_TITLE = "A bondade do Senhor";

Deno.test({
  name: "gig detail: remove button deletes the clicked row, not the first",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    page.setDefaultNavigationTimeout(15000);
    page.setDefaultTimeout(15000);
    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");
    const GOTO = { waitUntil: "domcontentloaded" as const };

    // ── 0. Create a grp and a gig linked to it ──────────────────────────
    await page.goto(`${BASE}/grp/add`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', `RemoveRow Grp ${Date.now()}`);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/, { timeout: 5000 });
    const grpId = page.url().split("/grp/")[1];

    await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
    await page.fill('input[name="title"]', `RemoveRow SB ${Date.now()}`);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/);
    const sbId = page.url().split("/gig/")[1].replace(/\/$/, "");

    // ── 1. Remove all auto-populated songs ──────────────────────────────
    await page.goto(`${BASE}/gig/${sbId}`, GOTO);
    await page.waitForSelector("body", { timeout: 5000 });
    for (let i = 0; i < 10; i++) {
      const rm = await page.$('[data-testid="remove-song-btn"]');
      if (!rm) break;
      await rm.click();
      await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 8000 });
    }
    await waitForText(page, "body", "No songs yet");

    // ── 2. Add three songs via the API (order matters: A, B, C) ────────
    async function addSong(songId: string): Promise<void> {
      const { token, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);
      const r = await fetch(`${BASE}/api/gig/${sbId}/songs`, {
        method: "POST",
        body: new URLSearchParams({
          song_id: songId,
          format: "any",
          csrf_token: token,
        }).toString(),
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
          Cookie: ch,
        },
        redirect: "manual",
      });
      if (r.status >= 400) throw new Error(`Seed ${songId} failed: ${r.status}`);
      await r.body?.cancel();
    }
    await addSong(SONG_A_ID);
    await addSong(SONG_B_ID);
    await addSong(SONG_C_ID);

    await page.goto(`${BASE}/gig/${sbId}`, GOTO);
    await waitForText(page, "body", SONG_A_TITLE);
    await waitForText(page, "body", SONG_B_TEXT);
    await waitForText(page, "body", SONG_C_TITLE);

    const items = await page.$$('[data-gig-item]');
    if (items.length !== 3)
      throw new Error(`Expected 3 songs before remove, got ${items.length}`);

    // ── 3. Click remove on the SECOND row (song B) ──────────────────────
    const removeBtns = page.locator('[data-testid="remove-song-btn"]');
    await Promise.all([
      page.waitForNavigation(),
      removeBtns.nth(1).click(),
    ]);

    // ── 4. Verify B is gone, A and C remain in order ────────────────────
    const afterItems = await page.$$('[data-gig-item]');
    if (afterItems.length !== 2)
      throw new Error(
        `Expected 2 songs after removing row 1, got ${afterItems.length}`,
      );

    const texts: string[] = [];
    for (const el of afterItems) texts.push((await el.textContent()) ?? "");
    if (!texts[0]?.includes(SONG_A_TITLE))
      throw new Error(`First remaining row should be song A, got: ${texts[0]}`);
    if (!texts[1]?.includes(SONG_C_TITLE))
      throw new Error(`Second remaining row should be song C, got: ${texts[1]}`);
    if (texts.some((t) => t.includes(SONG_B_TEXT)))
      throw new Error("Song B should have been removed but is still present");
  } finally {
    await browser.close();
  }
});
