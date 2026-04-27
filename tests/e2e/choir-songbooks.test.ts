/**
 * E2E test: choir detail — songbooks section
 *
 * Tests:
 *   1. Create a choir
 *   2. Create a songbook linked to that choir via /songbook/add?choir=<id>
 *   3. Navigate to the choir detail page
 *   4. Verify the "Songbooks" section lists the created songbook
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test({
  name: "choir detail: created songbook appears in songbooks section",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await page.route("**/_frsh/js/**", (route) => route.abort());
    await page.route("**/styles.css", (route) => route.abort());

    const GOTO = { waitUntil: "domcontentloaded" as const };

    await createAndLoginUser(page, BASE);
    const ts = Date.now();
    const choirTitle = `Test Choir SB ${ts}`;
    const sbTitle = `Test SB for Choir ${ts}`;

    // ── 1. Create choir ───────────────────────────────────────────────────────
    await page.goto(`${BASE}/choir/add`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', choirTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/choir\/[^/]+$/, { timeout: 5000 });

    const choirId = page.url().split("/choir/")[1];

    // ── 2. Create songbook linked to this choir ───────────────────────────────
    await page.goto(`${BASE}/songbook/add?choir=${choirId}`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', sbTitle);

    // Verify the choir hidden/pre-filled field is populated (if visible)
    const choirField = await page.$('input[name="choir"]');
    if (choirField) {
      const choirFieldVal = await choirField.inputValue();
      if (!choirFieldVal.includes(choirId)) {
        throw new Error(
          `Expected choir field to contain "${choirId}", got "${choirFieldVal}"`,
        );
      }
    }

    await page.click('button[type="submit"]');
    await page.waitForURL(/\/songbook\/[^/]+$/, { timeout: 5000 });

    const sbId = page.url().split("/songbook/")[1];

    // ── 3. Navigate to choir detail page ─────────────────────────────────────
    await page.goto(`${BASE}/choir/${choirId}`, GOTO);
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "body", choirTitle);

    // ── 4. Verify songbooks section lists the created songbook ────────────────
    await waitForText(page, "body", "Songbooks");
    await waitForText(page, "body", sbTitle);

    // Also verify the songbook link points to /songbook/<id>
    const sbLink = await page.$(`a[href="/songbook/${sbId}"]`);
    if (!sbLink) {
      throw new Error(
        `Expected link to /songbook/${sbId} on choir detail page, not found`,
      );
    }
  } finally {
    await browser.close();
  }
});
