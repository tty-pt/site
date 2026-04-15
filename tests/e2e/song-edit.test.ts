/**
 * E2E test: song edit happy path
 *
 * Creates a fresh song, edits its title, verifies the change, then cleans up.
 *
 * Requires both servers to be running:
 *   NDC on port 8080  (./start.sh)
 *   Fresh on port 3000 (deno task start)
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("song edit: login → add song → edit title → verify on detail page", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  const ts = Date.now();
  const originalTitle = `Song Edit Test ${ts}`;
  const editedTitle = `${originalTitle} edited`;
  let songId = "";

  try {
    await createAndLoginUser(page, BASE);

    // ── 1. Create the song and capture the resulting song ID ──────────────────
    await page.goto(`${BASE}/song/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', originalTitle);
    await page.click('button[type="submit"]');

    // Wait for navigation to /song/<id> and extract the id from the URL
    await page.waitForURL(`${BASE}/song/**`, { timeout: 5000 });
    const url = page.url();
    songId = url.replace(`${BASE}/song/`, "").replace(/\/$/, "");

    if (!songId) {
      throw new Error(`Could not extract song ID from URL: ${url}`);
    }

    // ── 2. Navigate to edit page ──────────────────────────────────────────────
    await page.goto(`${BASE}/song/${songId}/edit`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });

    // Verify pre-populated title
    const currentTitle = await page.inputValue('input[name="title"]');
    if (currentTitle !== originalTitle) {
      throw new Error(
        `Expected pre-filled title "${originalTitle}", got "${currentTitle}"`,
      );
    }

    // ── 3. Edit title and submit ──────────────────────────────────────────────
    await page.fill('input[name="title"]', editedTitle);
    await page.click('button[type="submit"]');

    // Should redirect to /song/:id after save
    await page.waitForURL(`${BASE}/song/${songId}`, { timeout: 5000 });

    // ── 4. Verify new title appears on the detail page ────────────────────────
    await page.waitForSelector("h1", { timeout: 5000 });
    const h1 = await page.textContent("h1");
    if (!h1?.includes(editedTitle)) {
      throw new Error(
        `Detail page h1 does not contain edited title.\nGot: "${h1}"`,
      );
    }
  } finally {
    await browser.close();

    // Cleanup: remove created song directory
    if (songId) {
      try {
        await Deno.remove(`items/song/items/${songId}`, { recursive: true });
      } catch {
        // Already gone or never created — ignore
      }
    }
  }
});
