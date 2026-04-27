/**
 * E2E test: song detail page UI changes
 *
 * Tests:
 *   1. Categories/author metadata row is displayed (when present)
 *   2. "Back to Songs" button is absent from .center
 *   3. Logged in as owner: edit + delete buttons appear in sidebar (.functions)
 *   4. Logged out: edit + delete buttons are absent
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
// A song known to exist in test data with a "type" (categories) set
const SONG_ID = "a_alegria_esta_no_coracao";
const SONG_TITLE = "A alegria está no coração";
const SONG_CATEGORY = "Animação";

Deno.test({
  name: "song detail: categories/author row shown; no back button; edit/delete in sidebar for owner only",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let createdSongId: string | null = null;

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await page.route("**/_frsh/js/**", (route) => route.abort());
    await page.route("**/styles.css", (route) => route.abort());

    const GOTO = { waitUntil: "domcontentloaded" as const };

    // ── 1. Logged out — verify metadata row + no back button ─────────────────
    await page.goto(`${BASE}/song/${SONG_ID}`, GOTO);
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "body", SONG_TITLE);

    // Categories should be visible
    await waitForText(page, "body", SONG_CATEGORY);

    // "Back to Songs" button inside .center should be gone
    const backInCenter = await page.$('.center a[href="/song/"]');
    if (backInCenter) {
      const text = await backInCenter.textContent();
      if (text?.toLowerCase().includes("back")) {
        throw new Error('Found "Back to Songs" link inside .center — it should have been removed');
      }
    }

    // ── 2. Logged in as owner: create song + verify owner sidebar buttons ─────
    await createAndLoginUser(page, BASE);

    // Use the browser form to create a song (avoids multipart/connection issues)
    const songTitle = `Detail UI Test ${Date.now()}`;
    await page.goto(`${BASE}/song/add`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', songTitle);

    // Fill author if the form has it
    const authorInput = await page.$('input[name="author"], textarea[name="author"]');
    if (authorInput) await authorInput.fill("Test Author");

    await page.click('button[type="submit"]');
    await page.waitForURL(/\/song\/[^/]+$/, { timeout: 5000 });

    const songId = page.url().split("/song/")[1]?.replace(/\/$/, "");
    if (!songId) throw new Error(`Could not parse song ID from URL: ${page.url()}`);
    createdSongId = songId;

    // Navigate to the song detail
    await page.goto(`${BASE}/song/${songId}`, GOTO);
    await page.waitForSelector("h1", { timeout: 5000 });

    // Edit button should appear in sidebar (.functions)
    const editInSidebar = await page.$(`.functions a[href="/song/${songId}/edit"]`);
    if (!editInSidebar) {
      throw new Error(`Expected edit button in .functions sidebar for owner, not found`);
    }

    // Delete button should appear in sidebar
    const deleteInSidebar = await page.$(`.functions a[href="/song/${songId}/delete"]`);
    if (!deleteInSidebar) {
      throw new Error(`Expected delete button in .functions sidebar for owner, not found`);
    }

    // ── 3. Logged out: edit/delete buttons absent ────────────────────────────
    await page.goto(`${BASE}/auth/logout`, GOTO);
    await page.waitForURL(/^\S+\/$/, { timeout: 5000 });

    await page.goto(`${BASE}/song/${songId}`, GOTO);
    await page.waitForSelector("h1", { timeout: 5000 });

    const editLoggedOut = await page.$(`.functions a[href="/song/${songId}/edit"]`);
    if (editLoggedOut) {
      throw new Error("Edit button should not appear in sidebar for non-owner/logged-out");
    }

    const deleteLoggedOut = await page.$(`.functions a[href="/song/${songId}/delete"]`);
    if (deleteLoggedOut) {
      throw new Error("Delete button should not appear in sidebar for non-owner/logged-out");
    }
  } finally {
    // Cleanup created song
    if (createdSongId) {
      try {
        await Deno.remove(`/home/quirinpa/site/items/song/items/${createdSongId}`, { recursive: true });
      } catch { /* ignore */ }
    }
    await browser.close();
  }
});
