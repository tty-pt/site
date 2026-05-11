/**
 * E2E test: song list pagination on initial page load
 *
 * Verifies that /song/ renders at most 10 rows (paginated), that the
 * total count is shown, and that navigating to page 2 shows different
 * results with correct page indicator.
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";

function extractTotal(text: string, label: string): number {
  const m = text.match(/(\d+) of (\d+) rows/);
  if (!m) {
    throw new Error(
      `Could not find "N of M rows" in ${label}: "${text}"`,
    );
  }
  return parseInt(m[2], 10);
}

Deno.test("song list: initial page shows 10 rows with pagination", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await page.goto(`${BASE}/song/`, { waitUntil: "load" });
    await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

    const rows = page.locator("tr.hyle-row-clickable");
    const count = await rows.count();

    if (count !== 10) {
      throw new Error(
        `Expected exactly 10 song rows on initial page, got ${count}`,
      );
    }

    const bodyText = await page.textContent("body") ?? "";
    const total = extractTotal(bodyText, "initial page");
    if (total <= 0) {
      throw new Error(
        `Expected total > 0 on initial page, got ${total}`,
      );
    }

    if (!bodyText.includes("Page 1")) {
      throw new Error('Expected "Page 1" text on initial page');
    }

    const firstHref = await rows.nth(0)
      .locator("td:first-child a")
      .getAttribute("href");

    await page.locator('button:has-text("Next")').click();
    await page.waitForURL(/page=2/, { timeout: 10000 });
    await page.waitForLoadState("load");
    await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

    const bodyP2 = await page.textContent("body") ?? "";
    if (!bodyP2.includes("Page 2")) {
      throw new Error('Expected "Page 2" text on page 2');
    }

    const prevDisabled = await page.locator(
      'button:has-text("Prev")',
    ).isDisabled();
    if (prevDisabled) {
      throw new Error(
        "Expected Prev button to be enabled on page 2, but it was disabled",
      );
    }

    const rows2 = page.locator("tr.hyle-row-clickable");
    const count2 = await rows2.count();
    if (count2 !== 10) {
      throw new Error(
        `Expected exactly 10 song rows on page 2, got ${count2}`,
      );
    }

    const secondHref = await rows2.nth(0)
      .locator("td:first-child a")
      .getAttribute("href");

    if (secondHref === firstHref) {
      throw new Error(
        "Expected first row on page 2 to differ from page 1, but they are the same",
      );
    }
  } finally {
    await browser.close();
  }
});
