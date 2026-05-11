/**
 * E2E test: songbook detail page — add/remove songs
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
  name: "songbook detail: add and remove songs via API",
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

    // ── 0. Create a choir and seed the known song ─────────────────────────
    const choirTitle = `EditRow Choir ${Date.now()}`;
    await page.goto(`${BASE}/choir/add`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', choirTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/choir\/[^/]+$/, { timeout: 5000 });
    const choirId = page.url().split("/choir/")[1];

    const { token: csrfSeed, cookieHeader: chSeed } = await getCsrfToken(cookieHeader, BASE);
    const seedResp = await fetch(`${BASE}/api/choir/${choirId}/songs`, {
      method: "POST",
      body: new URLSearchParams({ song_id: KNOWN_SONG_ID, format: "any", csrf_token: csrfSeed }).toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chSeed },
      redirect: "manual",
    });
    if (seedResp.status >= 400) throw new Error(`Seed failed: ${seedResp.status}`);
    await seedResp.body?.cancel();
    const repoId = `${choirId}_${KNOWN_SONG_ID}`;

    // ── 1. Create a songbook linked to the choir ──────────────────────────
    await page.goto(`${BASE}/songbook/add?choir=${choirId}`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', `SB AddRemove Test ${Date.now()}`);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/songbook\/[^/]+$/, { timeout: 5000 });
    sbId = page.url().split("/songbook/")[1];

    // ── 2. Navigate to detail page, remove all auto-populated songs ───────
    await page.goto(`${BASE}/songbook/${sbId}`, GOTO);
    await page.waitForSelector("body", { timeout: 5000 });
    for (let i = 0; i < 10; i++) {
      const rm = await page.$('[data-testid="remove-song-btn"]');
      if (!rm) break;
      await rm.click();
      await page.waitForURL(/\/songbook\/[^/]+$/, { timeout: 8000 });
    }
    await waitForText(page, "body", "No songs yet");

    // ── 3. Add a known song via the "Add Song" form ──────────────────────
    await page.waitForSelector('input[name="song_id"]', { timeout: 5000 });
    await page.fill('input[name="song_id"]', repoId);
    await page.click('button:has-text("Add Song")');
    await page.waitForURL(/\/songbook\/[^/]+$/, { timeout: 8000 });
    await waitForText(page, "body", KNOWN_SONG_TITLE);

    // ── 4. Verify chord data renders ──────────────────────────────────────
    await page.waitForSelector('[data-songbook-chord-data]', { timeout: 5000 });
    const chordData = await page.textContent('[data-songbook-chord-data]');
    if (!chordData || chordData.length < 10)
      throw new Error("Chord data too short or missing");

    // ── 5. Remove the song ────────────────────────────────────────────────
    await page.waitForSelector('[data-testid="remove-song-btn"]', { timeout: 5000 });
    await page.click('[data-testid="remove-song-btn"]');
    await page.waitForURL(/\/songbook\/[^/]+$/, { timeout: 8000 });
    await waitForText(page, "body", "No songs yet");

  } finally {
    await browser.close();
  }
});
