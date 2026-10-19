/**
 * E2E test: grp detail — gigs section
 *
 * Tests:
 *   1. Create a grp
 *   2. Create a gig linked to that grp via /gig/add?grp=<id>
 *   3. Navigate to the grp detail page
 *   4. Verify the "Gigs" section lists the created gig
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test({
  name: "grp detail: created gig appears in gigs section",
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
    const grpTitle = `Test Grp SB ${ts}`;
    const sbTitle = `Test SB for Grp ${ts}`;

    // ── 1. Create grp ───────────────────────────────────────────────────────
    await page.goto(`${BASE}/grp/add`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', grpTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/, { timeout: 5000 });

    const grpId = page.url().split("/grp/")[1];

    // ── 2. Create gig linked to this grp ───────────────────────────────
    await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', sbTitle);

    // Verify the grp hidden/pre-filled field is populated (if visible)
    const grpField = await page.$('input[name="grp"]');
    if (grpField) {
      const grpFieldVal = await grpField.inputValue();
      if (!grpFieldVal.includes(grpId)) {
        throw new Error(
          `Expected grp field to contain "${grpId}", got "${grpFieldVal}"`,
        );
      }
    }

    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 5000 });

    const sbId = page.url().split("/gig/")[1];

    // ── 3. Navigate to grp detail page ─────────────────────────────────────
    await page.goto(`${BASE}/grp/${grpId}`, GOTO);
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "body", grpTitle);

    // ── 4. Verify gigs section lists the created gig ────────────────
    await waitForText(page, "body", "Gigs");
    await waitForText(page, "body", sbTitle);

    // Also verify the gig link points to /gig/<id>
    const sbLink = await page.$(`a[href="/gig/${sbId}"]`);
    if (!sbLink) {
      throw new Error(
        `Expected link to /gig/${sbId} on grp detail page, not found`,
      );
    }
  } finally {
    await browser.close();
  }
});
