/**
 * E2E test: song deletion
 *
 * Tests:
 *   1. Owner can delete their song
 *   2. Deletion correctly removes the item from the list
 *   3. Deletion removes the item directory from the filesystem
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken, waitForText } from "./helpers/auth.ts";

const REPO_ROOT = new URL("../../", import.meta.url).pathname.replace(/\/$/, "");
const BASE = "http://localhost:8080";

Deno.test({
  name: "song deletion: owner can delete song and files are removed",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let songId: string | null = null;
  const songTitle = `Delete Test Song ${Date.now()}`;

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await createAndLoginUser(page, BASE);

    // 1. Create a song
    await page.goto(`${BASE}/song/add`);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', songTitle);
    await page.click('button[type="submit"]');

    await page.waitForURL(/\/song\/[^/]+$/);
    songId = page.url().split("/song/")[1].replace(/\/$/, "");

    // 2. Navigate to delete confirmation page
    await page.goto(`${BASE}/song/${songId}/delete`);
    await waitForText(page, "body", "Are you sure you want to delete");
    await waitForText(page, "body", songTitle);

    // 3. Perform deletion
    await page.click('button[type="submit"]');

    // Should redirect to song list
    await page.waitForURL(`${BASE}/song`);

    // 4. Verify song is gone from list
    const content = await page.textContent("body") ?? "";
    if (content.includes(songTitle)) {
      throw new Error("Song title still present in list after deletion");
    }

    // 5. Verify files are removed from disk
    const itemPath = `${REPO_ROOT}/items/song/items/${songId}`;
    try {
      await Deno.stat(itemPath);
      throw new Error(`Item directory ${itemPath} still exists after deletion`);
    } catch (e) {
      if (!(e instanceof Deno.errors.NotFound)) {
        throw e;
      }
    }

  } finally {
    await browser.close();
    if (songId) {
      try {
        await Deno.remove(`${REPO_ROOT}/items/song/items/${songId}`, { recursive: true });
      } catch { /* ignore */ }
    }
  }
});
