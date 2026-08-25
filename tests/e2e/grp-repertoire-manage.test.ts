/**
 * E2E test: grp repertoire management
 *
 * Tests:
 *   1. Add song to grp repertoire (via browser form)
 *   2. Set song key in grp repertoire (via browser form)
 *   3. Remove song from grp repertoire (via browser form)
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";
const SONG_TITLE = "A alegria está no coração";

Deno.test({
  name: "grp repertoire: add, set key, and remove song",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let grpId: string | null = null;

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await createAndLoginUser(page, BASE);

    // 1. Create a grp via browser form
    const grpTitle = `Repertoire Test Grp ${Date.now()}`;
    await page.goto(`${BASE}/grp/add`);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', grpTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/);
    grpId = page.url().split("/grp/")[1].replace(/\/$/, "");

    // 2. Add song to grp repertoire via the list-grade picker:
    //    search (Enter submits — omni mode hides Apply), then click
    //    the row (whole-row submit)
    await page.waitForSelector('form.list-form input[name="q"]');
    await page.fill('form.list-form input[name="q"]', SONG_TITLE);
    await page.press('form.list-form input[name="q"]', "Enter");
    await page.waitForSelector("button.hyle-row-action", { timeout: 8000 });
    await page.click("button.hyle-row-action >> nth=0");
    // Wait for the remove button to appear (confirms song added + redirect complete)
    await page.waitForSelector('button:has-text("Remove")', { timeout: 8000 });

    // 3. Set song key via the key selector
    await page.waitForSelector('select[name="key"]');
    await page.selectOption('select[name="key"]', "5");
    await page.click('button:has-text("Set")');
    // Wait for redirect to complete (remove button should still be there)
    await page.waitForSelector('button:has-text("Remove")', { timeout: 8000 });

    // 4. Remove song from grp repertoire
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
