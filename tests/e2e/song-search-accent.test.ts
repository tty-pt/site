/**
 * E2E test: accent-insensitive search on song listing
 *
 * Verifies that the title filter matches accented characters whether the
 * user types them accented or not:
 * 1. searching "Coração" (accented) returns "Coração Adorador" and only
 *    rows whose title contains "cora"
 * 2. searching "coracao" (unaccented) returns rows matching the same
 *    property (accent folding)
 * 3. searching a non-matching value returns 0 rows
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";

Deno.test("song list: accent-insensitive title search", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await page.goto(`${BASE}/song/`, { waitUntil: "load" });
    await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

    // ---- 1. Accented query matches accented data ----
    await page.locator('input[name="title"]').fill("Coração");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/title=Cora/, { timeout: 10000 });
    await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

    let rows = page.locator("tr.hyle-row-clickable");
    const countAccented = await rows.count();
    if (countAccented === 0) {
      throw new Error(
        'Expected rows for title="Coração", got 0',
      );
    }

    const titlesAccented = await rows.locator("td:first-child")
      .allTextContents();
    for (const t of titlesAccented) {
      if (!/cora/i.test(t)) {
        throw new Error(
          `Expected every row to contain "cora" for accented query, got: "${t}"`,
        );
      }
    }
    if (!titlesAccented.join("|").includes("Coração Adorador")) {
      throw new Error(
        'Expected "Coração Adorador" among accented-query results',
      );
    }

    // ---- 2. Unaccented query matches accented data ----
    await page.locator('input[name="title"]').fill("coracao");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/title=coracao/, { timeout: 10000 });
    await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

    rows = page.locator("tr.hyle-row-clickable");
    if (await rows.count() === 0) {
      throw new Error(
        'Expected rows for title="coracao", got 0',
      );
    }

    const titlesUnaccented = await rows.locator("td:first-child")
      .allTextContents();
    for (const t of titlesUnaccented) {
      if (!/cora/i.test(t)) {
        throw new Error(
          `Expected every row to contain "cora" for unaccented query, got: "${t}"`,
        );
      }
    }
    if (!titlesUnaccented.join("|").includes("Coração Adorador")) {
      throw new Error(
        'Expected "Coração Adorador" among unaccented-query results',
      );
    }

    // ---- 3. Non-matching value returns 0 rows ----
    await page.locator('input[name="title"]').fill("zzzzzz");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/title=zzzzzz/, { timeout: 10000 });
    await page.waitForSelector("div.hyle-table-wrap", { timeout: 10000 });

    const content = await page.content();
    if (!content.includes("0 of 0 rows")) {
      throw new Error(
        'Expected "0 of 0 rows" for non-matching title, got something else',
      );
    }
  } finally {
    await browser.close();
  }
});
