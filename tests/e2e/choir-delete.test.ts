/**
 * E2E test: choir deletion
 *
 * Tests:
 *   1. Owner can delete their choir
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
  name: "choir deletion: owner can delete choir and files are removed",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let choirId: string | null = null;
  const choirTitle = `Delete Test Choir ${Date.now()}`;

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await createAndLoginUser(page, BASE);

    // 1. Create a choir
    await page.goto(`${BASE}/choir/add`);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', choirTitle);
    await page.click('button[type="submit"]');

    await page.waitForURL(/\/choir\/[^/]+$/);
    choirId = page.url().split("/choir/")[1].replace(/\/$/, "");

    // 2. Navigate to delete confirmation page
    await page.goto(`${BASE}/choir/${choirId}/delete`);
    await waitForText(page, "body", "Are you sure you want to delete");
    await waitForText(page, "body", choirTitle);

    // 3. Perform deletion
    await page.click('button[type="submit"]');

    // Should redirect to choir list
    await page.waitForURL(`${BASE}/choir`);

    // 4. Verify choir is gone from list
    const content = await page.textContent("body") ?? "";
    if (content.includes(choirTitle)) {
      throw new Error("Choir title still present in list after deletion");
    }

    // 5. Verify files are removed from disk
    const itemPath = `${REPO_ROOT}/items/choir/items/${choirId}`;
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
    if (choirId) {
      try {
        await Deno.remove(`${REPO_ROOT}/items/choir/items/${choirId}`, { recursive: true });
      } catch { /* ignore */ }
    }
  }
});
