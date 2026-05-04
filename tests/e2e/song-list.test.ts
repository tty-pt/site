/**
 * E2E test: song list page
 *
 * Verifies that /song/ renders and shows song entries.
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";

Deno.test("song list: /song/ renders with song links", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    // Navigate to /song/ — no auth required for list view
    await page.goto(`${BASE}/song/`, { waitUntil: "domcontentloaded" });

    // Wait for the table rows to render (HyleTablePanel renders rowClickable rows)
    await page.waitForSelector("tr.rowClickable", { timeout: 5000 });

    // Should contain at least one song link (there are 440+ songs in the fixture data)
    const links = await page.$$('tr.rowClickable a[href^="/song/"]');
    if (links.length === 0) {
      throw new Error("Expected at least one song link on /song/, found none.");
    }

    // Verify the first link points to /song/<id>/
    const firstHref = await links[0].getAttribute("href");
    if (!firstHref?.startsWith("/song/")) {
      throw new Error(
        `Expected first song link to start with "/song/", got: "${firstHref}"`,
      );
    }
  } finally {
    await browser.close();
  }
});
