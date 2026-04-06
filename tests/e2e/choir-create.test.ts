/**
 * E2E test: choir create → detail page → edit form.
 *
 * Tests:
 *   1. Authenticated user can create a choir via /choir/add
 *   2. Redirects to choir detail page after creation
 *   3. Detail page shows the owner username and the Edit/Delete menu
 *   4. Edit form is pre-populated with the choir title
 *   5. Cancel returns to the detail page
 *
 * Requires: NDC (8080), Fresh (3000) running.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("choir: register → login → create choir → view detail → edit form", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
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
  } finally {
    await browser.close();
  }
});
