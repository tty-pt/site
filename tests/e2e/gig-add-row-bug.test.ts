/**
 * E2E test: gig detail — add multiple songs
 *
 * Tests:
 *   1. Remove all auto-populated songs → empty state
 *   2. Add two songs via the "Add Song" form
 *   3. Verify both songs appear on the detail page
 *   4. Remove the first song
 *   5. Verify the second song still remains
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

Deno.test({
  name: "gig detail: add multiple songs, remove first, verify second survives",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let sbId: string | null = null;

  try {
    page.setDefaultNavigationTimeout(15000);
    page.setDefaultTimeout(15000);
    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");
    const GOTO = { waitUntil: "domcontentloaded" as const };

    // ── 0. Create a grp and seed two songs ──────────────────────────────
    const grpTitle = `MultiSong Grp ${Date.now()}`;
    await page.goto(`${BASE}/grp/add`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', grpTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/, { timeout: 5000 });
    const grpId = page.url().split("/grp/")[1];

    let { token, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);
    let r1 = await fetch(`${BASE}/api/grp/${grpId}/songs`, {
      method: "POST",
      body: new URLSearchParams({ song_id: SONG_A_ID, format: "any", csrf_token: token }).toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: ch },
      redirect: "manual",
    });
    if (r1.status >= 400) throw new Error(`Seed A failed: ${r1.status}`);
    await r1.body?.cancel();

    ({ token, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE));
    let r2 = await fetch(`${BASE}/api/grp/${grpId}/songs`, {
      method: "POST",
      body: new URLSearchParams({ song_id: SONG_B_ID, format: "any", csrf_token: token }).toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: ch },
      redirect: "manual",
    });
    if (r2.status >= 400) throw new Error(`Seed B failed: ${r2.status}`);
    await r2.body?.cancel();

    // ── 1. Create a gig linked to the grp ──────────────────────────
    await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
    await page.fill('input[name="title"]', `MultiSong SB ${Date.now()}`);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/);
    sbId = page.url().split("/gig/")[1].replace(/\/$/, "");

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

    // ── 3. Add song A via the list-grade picker (Enter submits the
    //       omni search; whole-row button carries the song id) ───────────
    await page.waitForSelector('form.list-form input[name="q"]', { timeout: 5000 });
    await page.fill('form.list-form input[name="q"]', SONG_A_TITLE);
    await page.press('form.list-form input[name="q"]', "Enter");
    const pickA = `button.hyle-row-action[value="${SONG_A_ID}"]`;
    await page.waitForSelector(pickA, { timeout: 8000 });
    await page.click(pickA);
    await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 8000 });
    await waitForText(page, "body", SONG_A_TITLE);

    // ── 4. Add song B via the picker as well ─────────────────────────────
    await page.fill('form.list-form input[name="q"]', SONG_B_TEXT);
    await page.press('form.list-form input[name="q"]', "Enter");
    const pickB = `button.hyle-row-action[value="${SONG_B_ID}"]`;
    await page.waitForSelector(pickB, { timeout: 8000 });
    await page.click(pickB);
    await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 8000 });
    await waitForText(page, "body", SONG_A_TITLE);
    await waitForText(page, "body", SONG_B_TEXT);

    // Count song entries
    const beforeItems = await page.$$('[data-gig-item]');
    if (beforeItems.length !== 2)
      throw new Error(`Expected 2 song entries before remove, got ${beforeItems.length}`);

    // ── 5. Remove first song (song A at index 0) ─────────────────────────
    await page.waitForSelector('[data-testid="remove-song-btn"]', { timeout: 5000 });
    await page.click('[data-testid="remove-song-btn"]');
    await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 8000 });

    // ── 6. Verify one song remains, and it's song B ──────────────────────
    const afterItems = await page.$$('[data-gig-item]');
    if (afterItems.length !== 1)
      throw new Error(`Expected 1 song entry after remove, got ${afterItems.length}`);

    const itemText = await afterItems[0].textContent();
    if (!itemText?.includes(SONG_B_TEXT))
      throw new Error("Remaining song should be song B");
  } finally {
    await browser.close();
  }
});
