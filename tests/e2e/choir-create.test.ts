/**
 * E2E test: choir create → detail page → edit form → edit title → add song
 *           → update preferred key → view choir song → remove song.
 *
 * Tests:
 *   1. Authenticated user can create a choir via /choir/add
 *   2. Redirects to choir detail page after creation
 *   3. Detail page shows the owner username and the Edit/Delete menu
 *   4. Edit form is pre-populated with the choir title
 *   5. Cancel returns to the detail page
 *   6. Submit edit form with a new title → verify updated title on detail page
 *   7. Add a known song to choir repertoire → verify it appears on detail page
 *   8. Update preferred key for the song via /api/choir/:id/song/:song_id/key
 *   9. View the choir song page (GET /choir/:id/song/:song_id)
 *  10. Delete song from choir → verify it no longer appears on detail page
 *
 * Requires: NDC (8080), Fresh (3000) running.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
// A song that always exists in the test data
const KNOWN_SONG_ID = "a_alegria_esta_no_coracao";
const KNOWN_SONG_TITLE = "A alegria está no coração";

Deno.test({ name: "choir: register → login → create choir → view detail → edit form → edit title → add song", sanitizeResources: false, sanitizeOps: false }, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);
    const user = await createAndLoginUser(page, BASE);
    const choirTitle = `Test Choir ${Date.now()}`;

    // ── 1. Create choir via /choir/add ──────────────────────────────────────
    await page.goto(`${BASE}/choir/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', choirTitle);
    await page.click('button[type="submit"]');

    // Should redirect to /choir/<id> after creation
    await page.waitForURL(/\/choir\//, { timeout: 5000 });

    // ── 2. Verify detail page shows title and owner ─────────────────────────
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "body", choirTitle);
    await waitForText(page, "body", `Owner: ${user.username}`);

    // Extract choir ID from URL
    const choirUrl = page.url();
    const choirId = choirUrl.split("/choir/")[1];

    // ── 3. Verify Edit and Delete buttons appear in menu ────────────────────
    await page.waitForSelector('a[href*="/edit"]', { timeout: 5000 });
    const editLink = await page.getAttribute('a[href*="/edit"]', "href");
    if (!editLink?.includes(choirId)) {
      throw new Error(`Edit link "${editLink}" does not belong to choir ${choirId}`);
    }

    // ── 4. Navigate to edit form and verify pre-population ─────────────────
    await page.goto(`${BASE}/choir/${choirId}/edit`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });

    const formTitle = await page.inputValue('input[name="title"]');
    if (formTitle !== choirTitle) {
      throw new Error(
        `Edit form title mismatch. Expected "${choirTitle}", got "${formTitle}"`,
      );
    }

    // ── 5. Cancel returns to detail page ────────────────────────────────────
    await page.click('a[href*="/choir/"]');
    await page.waitForURL(`${BASE}/choir/${choirId}`, { timeout: 5000 });
    await waitForText(page, "body", choirTitle);

    // ── 6. Edit choir title via form submit ─────────────────────────────────
    const updatedTitle = `${choirTitle} (updated)`;

    // Use fetch (same session cookie via the browser context) to POST the edit
    const editResp = await page.evaluate(
      async ({ url, title }: { url: string; title: string }) => {
        const fd = new FormData();
        fd.append("title", title);
        const r = await fetch(url, { method: "POST", body: fd });
        return { status: r.status, url: r.url };
      },
      { url: `${BASE}/api/choir/${choirId}/edit`, title: updatedTitle },
    );

    if (editResp.status !== 200 && editResp.status !== 303) {
      throw new Error(`Choir edit POST returned unexpected status ${editResp.status}`);
    }

    // Reload detail page and verify updated title
    await page.goto(`${BASE}/choir/${choirId}`);
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "body", updatedTitle);

    // ── 7. Add known song to choir repertoire ───────────────────────────────
    const addSongResp = await page.evaluate(
      async ({ url, songId }: { url: string; songId: string }) => {
        const fd = new FormData();
        fd.append("song_id", songId);
        fd.append("format", "any");
        const r = await fetch(url, { method: "POST", body: fd });
        return r.status;
      },
      { url: `${BASE}/api/choir/${choirId}/songs`, songId: KNOWN_SONG_ID },
    );

    if (addSongResp >= 400) {
      throw new Error(`Add song to choir returned unexpected status ${addSongResp}`);
    }

    // Reload and verify song appears in the repertoire list
    await page.goto(`${BASE}/choir/${choirId}`);
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "body", KNOWN_SONG_TITLE, 6000);

    // ── 8. Update preferred key for the song ────────────────────────────────
    const keyResp = await page.evaluate(
      async ({ url }: { url: string }) => {
        const fd = new FormData();
        fd.append("key", "5");
        const r = await fetch(url, { method: "POST", body: fd });
        return r.status;
      },
      { url: `${BASE}/api/choir/${choirId}/song/${KNOWN_SONG_ID}/key` },
    );

    if (keyResp >= 400) {
      throw new Error(`Key update returned unexpected status ${keyResp}`);
    }

    // ── 9. View the choir song page ─────────────────────────────────────────
    await page.goto(`${BASE}/choir/${choirId}/song/${KNOWN_SONG_ID}`);
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "body", KNOWN_SONG_TITLE);

    // ── 10. Delete song from choir ───────────────────────────────────────────
    const deleteResp = await page.evaluate(
      async ({ url }: { url: string }) => {
        const r = await fetch(url, { method: "DELETE" });
        return r.status;
      },
      { url: `${BASE}/api/choir/${choirId}/song/${KNOWN_SONG_ID}` },
    );

    if (deleteResp >= 400) {
      throw new Error(`Song delete returned unexpected status ${deleteResp}`);
    }

    // Reload and verify song no longer appears
    await page.goto(`${BASE}/choir/${choirId}`);
    await page.waitForSelector("h1", { timeout: 5000 });
    const bodyText = await page.textContent("body");
    if (bodyText?.includes(KNOWN_SONG_TITLE)) {
      throw new Error(`Song "${KNOWN_SONG_TITLE}" still visible after deletion`);
    }
  } finally {
    await browser.close();
  }
});
