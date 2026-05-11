/**
 * E2E test: choir repertoire management
 *
 * Tests:
 *   1. Add song to choir repertoire (via browser form)
 *   2. Set song key in choir repertoire (via browser form)
 *   3. Remove song from choir repertoire (via browser form)
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";
const SONG_TITLE = "A alegria está no coração";

Deno.test({
  name: "choir repertoire: add, set key, and remove song",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let choirId: string | null = null;

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await createAndLoginUser(page, BASE);

    // 1. Create a choir via browser form
    const choirTitle = `Repertoire Test Choir ${Date.now()}`;
    await page.goto(`${BASE}/choir/add`);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', choirTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/choir\/[^/]+$/);
    choirId = page.url().split("/choir/")[1].replace(/\/$/, "");

    // 2. Add song to choir repertoire via the searchable input + datalist
    await page.waitForSelector('input[name="song_id"]');
    await page.fill('input[name="song_id"]', SONG_ID);
    await page.click('button:has-text("Add Song")');
    // Wait for the remove button to appear (confirms song added + redirect complete)
    await page.waitForSelector('button:has-text("Remove")', { timeout: 8000 });

    // 3. Set song key via the key selector
    await page.waitForSelector('select[name="key"]');
    await page.selectOption('select[name="key"]', "5");
    await page.click('button:has-text("Set")');
    // Wait for redirect to complete (remove button should still be there)
    await page.waitForSelector('button:has-text("Remove")', { timeout: 8000 });

    // 4. Remove song from choir repertoire
    await page.click('button:has-text("Remove")');
    // Wait for "No songs in repertoire yet" to appear
    await waitForText(page, "body", "No songs in repertoire yet");
    // Double-check no remove button remains
    const hasRemove = await page.$('button:has-text("Remove")');
    if (hasRemove) {
      throw new Error("Remove button still present after removal");
    }

  } finally {
    await browser.close();
  }
});
