/**
 * E2E test: songbook deletion
 *
 * Tests:
 *   1. Owner can delete their songbook
 *   2. Deletion correctly removes the item from the list
 *   3. Deletion removes the item directory from the filesystem
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const REPO_ROOT = new URL("../../", import.meta.url).pathname.replace(/\/$/, "");
const BASE = "http://localhost:8080";

Deno.test({
  name: "songbook deletion: owner can delete songbook and files are removed",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let sbId: string | null = null;
  const sbTitle = `Delete Test Songbook ${Date.now()}`;

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await createAndLoginUser(page, BASE);

    // 1. Create a songbook
    await page.goto(`${BASE}/songbook/add`);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', sbTitle);
    await page.click('button[type="submit"]');

    await page.waitForURL(/\/songbook\/[^/]+$/);
    sbId = page.url().split("/songbook/")[1].replace(/\/$/, "");

    // 2. Navigate to delete confirmation page
    await page.goto(`${BASE}/songbook/${sbId}/delete`);
    await waitForText(page, "body", "Are you sure you want to delete");
    await waitForText(page, "body", sbTitle);

    // 3. Perform deletion
    await page.click('button[type="submit"]');

    // Should redirect to songbook list
    await page.waitForURL(`${BASE}/songbook`);

    // 4. Verify songbook is gone from list
    const content = await page.textContent("body") ?? "";
    if (content.includes(sbTitle)) {
      throw new Error("Songbook title still present in list after deletion");
    }

    // 5. Verify files are removed from disk
    const itemPath = `${REPO_ROOT}/items/songbook/items/${sbId}`;
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
    if (sbId) {
      try {
        await Deno.remove(`${REPO_ROOT}/items/songbook/items/${sbId}`, { recursive: true });
      } catch { /* ignore */ }
    }
  }
});
