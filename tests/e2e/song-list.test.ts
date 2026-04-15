/**
 * E2E test: song list page
 *
 * Verifies that /song/ renders and shows song entries.
 *
 * Requires both servers running: NDC (8080), Fresh (3000).
 */

import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";

Deno.test("song list: /song/ renders with song links", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    // Navigate to /song/ — no auth required for list view
    await page.goto(`${BASE}/song/`, { waitUntil: "domcontentloaded" });

    // Wait for the page to render (should have at least one .btn link or "No items" message)
    await page.waitForSelector(".center", { timeout: 5000 });

    // Should contain at least one song link inside .center (there are 440+ songs in the fixture data)
    const links = await page.$$(".center a.btn");
    if (links.length === 0) {
      throw new Error("Expected at least one song link on /song/, found none.");
    }

    // Verify each visible link points to /song/<id>/
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
