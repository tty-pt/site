/**
 * E2E test: SSR filter + paginate on song listing
 *
 * Verifies that the C-side filter+pagination pipeline works:
 * 1. initial page shows 10 paginated rows (not all rows)
 * 2. initial total > filtered total (filter reduces results)
 * 3. clicking a type checkbox (Comunhão) and Apply returns 10 filtered rows
 * 4. filtered total is smaller than unfiltered total
 * 5. clicking Next shows page 2 with "Page 2" indicator (not "Page 1")
 * 6. Prev button is enabled on page 2 (not disabled)
 * 7. page 2's first result differs from page 1's
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";

function extractCount(text: string, label: string): number {
  const m = text.match(/(\d+) of (\d+) rows/);
  if (!m) {
    throw new Error(
      `Could not find "X of Y rows" in ${label}: "${text}"`,
    );
  }
  return parseInt(m[2], 10);
}

Deno.test("song list: filter by type and paginate across pages", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    console.log("Navigating to", `${BASE}/song/`);
    await page.goto(`${BASE}/song/`, { waitUntil: "domcontentloaded" });
    console.log("Page URL after goto:", page.url());

    await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
    console.log("Rows found!");

    // ---- Initial page: 10 rows, total > 0 ----
    const rowsInit = page.locator("tr.hyle-row-clickable");
    const countInit = await rowsInit.count();
    console.log("Initial row count:", countInit);
    if (countInit !== 10) {
      throw new Error(
        `Expected exactly 10 rows on initial page, got ${countInit}`,
      );
    }

    let initContent = await page.content();
    let totalInit = extractCount(initContent, "initial page");
    console.log("Initial total:", totalInit);
    if (totalInit <= 0) {
      throw new Error(
        `Expected initial total > 0, got ${totalInit}`,
      );
    }

    // ---- Filter: click Comunhão, Apply ----
    await page.locator(
      'input[type="checkbox"][name="type"][value="comunhao"]',
    ).click();
    console.log("Checkbox clicked");

    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    console.log("Apply clicked, waiting for navigation...");

    await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
    console.log("Filtered rows found, URL:", page.url());

    // ---- Page 1 filtered: 10 rows, all Comunhão, total < unfiltered ----
    const rowsP1 = page.locator("tr.hyle-row-clickable");
    const countP1 = await rowsP1.count();
    console.log("Filtered row count:", countP1);
    if (countP1 !== 10) {
      throw new Error(
        `Expected exactly 10 filtered rows on page 1, got ${countP1}`,
      );
    }

    const p1Content = await page.content();
    const totalFiltered = extractCount(p1Content, "filtered page 1");
    console.log("Filtered total:", totalFiltered);
    if (totalFiltered >= totalInit) {
      throw new Error(
        `Expected filtered total < initial total (${totalFiltered} >= ${totalInit})`,
      );
    }

    const typesP1 = await rowsP1.locator("td[data-label='Type']")
      .allTextContents();
    for (const t of typesP1) {
      if (!t.includes("Comunhão")) {
        throw new Error(
          `Expected filtered row to contain "Comunhão" in Type, got: "${t}"`,
        );
      }
    }

    const firstHrefP1 = await rowsP1.nth(0)
      .locator("td[data-label='Title'] a")
      .getAttribute("href");

    // ---- Page 2: indicator, Prev enabled, rows Comunhão, ----
    await page.locator('button:has-text("Next")').click();
    console.log("Next clicked, waiting for page 2...");
    await page.waitForURL(/page=2/, { timeout: 10000 });
    await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
    console.log("Page 2 loaded, URL:", page.url());

    const p2Content = await page.content();
    if (!p2Content.includes(">Page 2<")) {
      throw new Error(
        `Expected "Page 2" indicator on page 2, got something else`,
      );
    }

    const prevDisabled = await page.locator(
      'button:has-text("Prev")',
    ).isDisabled();
    if (prevDisabled) {
      throw new Error(
        "Expected Prev button to be enabled on page 2, but it was disabled",
      );
    }

    const rowsP2 = page.locator("tr.hyle-row-clickable");
    const countP2 = await rowsP2.count();
    console.log("Page 2 row count:", countP2);
    if (countP2 === 0) {
      throw new Error("Expected at least 1 filtered row on page 2");
    }
    if (countP2 > 10) {
      throw new Error(
        `Expected at most 10 filtered rows on page 2, got ${countP2}`,
      );
    }

    const typesP2 = await rowsP2.locator("td[data-label='Type']")
      .allTextContents();
    for (const t of typesP2) {
      if (!t.includes("Comunhão")) {
        throw new Error(
          `Expected filtered row on page 2 to contain "Comunhão", got: "${t}"`,
        );
      }
    }

    const firstHrefP2 = await rowsP2.nth(0)
      .locator("td[data-label='Title'] a")
      .getAttribute("href");

    if (firstHrefP2 === firstHrefP1) {
      throw new Error(
        "Expected first result on page 2 to differ from page 1, but they are the same",
      );
    }
  } finally {
    await browser.close();
  }
});
