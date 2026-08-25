/**
 * E2E test: grp deletion
 *
 * Tests:
 *   1. Owner can delete their grp
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
  name: "grp deletion: owner can delete grp and files are removed",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let grpId: string | null = null;
  const grpTitle = `Delete Test Grp ${Date.now()}`;

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await createAndLoginUser(page, BASE);

    // 1. Create a grp
    await page.goto(`${BASE}/grp/add`);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', grpTitle);
    await page.click('form[method="POST"] button[type="submit"]');

    await page.waitForURL(/\/grp\/[^/]+$/);
    grpId = page.url().split("/grp/")[1].replace(/\/$/, "");

    // 2. Navigate to delete confirmation page
    await page.goto(`${BASE}/grp/${grpId}/delete`);
    await waitForText(page, "body", "Are you sure you want to delete");
    await waitForText(page, "body", grpTitle);

    // 3. Perform deletion
    await page.click('form[method="POST"] button[type="submit"]');

    // Should redirect to grp list
    await page.waitForURL(`${BASE}/grp`);

    // 4. Verify grp is gone from list
    const content = await page.textContent("body") ?? "";
    if (content.includes(grpTitle)) {
      throw new Error("Grp title still present in list after deletion");
    }

    // 5. Verify files are removed from disk
    const itemPath = `${REPO_ROOT}/var/grp/${grpId}`;
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
    if (grpId) {
      try {
        await Deno.remove(`${REPO_ROOT}/var/grp/${grpId}`, { recursive: true });
      } catch { /* ignore */ }
    }
  }
});
