/**
 * E2E test: song add happy path
 *
 * Tests:
 *   1. Authenticated user can create a song via /song/add
 *   2. After submission, redirects to /song/<id> detail page
 *   3. Detail page shows the song title
 *
 * Requires: NDC (8080), Fresh (3000) running.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("song: login → add song → verify detail page", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  // Use a simple ASCII title so index_id() derivation is predictable:
  // spaces → underscores, lowercase → same. "Test Song E2E" → "test_song_e2e"
  const songTitle = "Test Song E2E";
  const expectedId = "test_song_e2e";

  try {
    await createAndLoginUser(page, BASE);

    // ── 1. Navigate to /song/add ────────────────────────────────────────────
    await page.goto(`${BASE}/song/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });

    // ── 2. Fill title and submit ────────────────────────────────────────────
    await page.fill('input[name="title"]', songTitle);
    await page.click('button[type="submit"]');

    // Should redirect to /song/<id> after creation
    await page.waitForURL(`${BASE}/song/${expectedId}`, { timeout: 5000 });

    // ── 3. Verify detail page shows the title ───────────────────────────────
    await page.waitForSelector("h1", { timeout: 5000 });
    const h1 = await page.textContent("h1");
    if (!h1?.includes(songTitle)) {
      throw new Error(
        `Detail page h1 does not contain song title.\nExpected: "${songTitle}"\nGot: "${h1}"`,
      );
    }
  } finally {
    await browser.close();

    // ── Cleanup: remove created song directory ─────────────────────────────
    try {
      await Deno.remove(`items/song/items/${expectedId}`, { recursive: true });
    } catch {
      // Already gone or never created — ignore
    }
  }
});
