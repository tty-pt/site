/**
 * E2E test: songbook create → view detail page.
 *
 * Tests:
 *   1. Authenticated user can create a songbook via POST /songbook/add
 *   2. GET /songbook/<id> renders the title
 *   3. Owner sees the "Edit Songbook" link
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("songbook: register → login → create songbook → view detail page", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  let sbId: string | null = null;

  try {
    await createAndLoginUser(page, BASE);
    const sbTitle = `Test Songbook ${Date.now()}`;

    // ── 1. Create songbook via /songbook/add ────────────────────────────────
    await page.goto(`${BASE}/songbook/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', sbTitle);
    await page.click('button[type="submit"]');

    // Should redirect to /songbook/<id> after creation
    await page.waitForURL(/\/songbook\/[^/]+$/, { timeout: 5000 });

    // Extract songbook ID from URL
    const sbUrl = page.url();
    sbId = sbUrl.split("/songbook/")[1];

    // ── 2. Verify detail page shows the title ───────────────────────────────
    await page.waitForSelector("body", { timeout: 5000 });
    await waitForText(page, "body", sbTitle);

    // ── 3. Owner sees the Edit Songbook link ────────────────────────────────
    await page.waitForSelector('a[href*="/edit"]', { timeout: 5000 });
    const editLink = await page.getAttribute('a[href*="/edit"]', "href");
    if (!editLink?.includes(sbId)) {
      throw new Error(
        `Edit link "${editLink}" does not belong to songbook ${sbId}`,
      );
    }
  } finally {
    // Cleanup: remove the created songbook directory
    if (sbId) {
      try {
        const sbPath = `/home/quirinpa/site/items/songbook/items/${sbId}`;
        for await (const entry of Deno.readDir(sbPath)) {
          await Deno.remove(`${sbPath}/${entry.name}`);
        }
        await Deno.remove(sbPath);
      } catch {
        // ignore cleanup errors
      }
    }
    await browser.close();
  }
});
