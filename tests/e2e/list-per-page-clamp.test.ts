/**
 * E2E test: song list per_page is bounded without destabilizing the server.
 *
 * Uses the existing corpus; no fixture rows are created.
 *
 * Requires: axil running on :8080.
 */

import { chromium, type Page } from "npm:playwright";

const BASE = "http://localhost:8080";
const ROWS = "tr.hyle-row-clickable";

async function gotoOk(page: Page, path: string): Promise<void> {
  const response = await page.goto(`${BASE}${path}`, { waitUntil: "load" });
  if (!response || response.status() >= 400) {
    throw new Error(
      `${path} did not respond successfully (status ${
        response?.status() ?? "none"
      })`,
    );
  }
}

async function totalRows(page: Page, label: string): Promise<number> {
  const text = await page.locator("body").textContent() ?? "";
  const match = text.match(/\d+ of (\d+) rows/);
  if (!match) {
    throw new Error(`Could not find the total row count on ${label}`);
  }
  return Number(match[1]);
}

async function rowHrefs(page: Page): Promise<string[]> {
  return await page.locator(ROWS)
    .locator("td:first-child a:not(.hyle-row-action)")
    .evaluateAll((links) =>
      links.map((link) => link.getAttribute("href") ?? "")
    );
}

Deno.test("song list: clamps per_page and keeps pagination healthy", async () => {
  const browser = await chromium.launch();
  const context = await browser.newContext({ javaScriptEnabled: false });
  const page = await context.newPage();

  try {
    page.setDefaultNavigationTimeout(10000);

    for (const perPage of [300, 1000]) {
      await gotoOk(page, `/song/?per_page=${perPage}`);
      const count = await page.locator(ROWS).count();
      if (count === 0 || count > 256) {
        throw new Error(
          `Expected 1..256 rows for per_page=${perPage}, got ${count}`,
        );
      }
    }

    // Oversized list requests must not leave the server unable to serve pages.
    await gotoOk(page, "/song/");

    await gotoOk(page, "/song/?per_page=10&page=1");
    const total = await totalRows(page, "unfiltered page 1");
    const page1Count = await page.locator(ROWS).count();
    const expectedPage1 = Math.min(10, total);
    if (page1Count !== expectedPage1) {
      throw new Error(
        `Expected ${expectedPage1} rows on page 1, got ${page1Count}`,
      );
    }
    const page1Hrefs = await rowHrefs(page);

    await gotoOk(page, "/song/?per_page=10&page=2");
    const page2Count = await page.locator(ROWS).count();
    const expectedPage2 = Math.min(10, Math.max(0, total - 10));
    if (page2Count !== expectedPage2) {
      throw new Error(
        `Expected ${expectedPage2} rows on page 2, got ${page2Count}`,
      );
    }

    const page2Hrefs = await rowHrefs(page);
    if (page1Hrefs.length > 0 && page2Hrefs.length > 0) {
      const overlap = page2Hrefs.filter((href) => page1Hrefs.includes(href));
      if (overlap.length > 0) {
        throw new Error(
          `Expected page 1 and page 2 to differ; repeated rows: ${
            overlap.join(", ")
          }`,
        );
      }
    }

    await gotoOk(page, "/song/?type=comunhao&per_page=1000");
    const filteredRows = page.locator(ROWS);
    const filteredCount = await filteredRows.count();
    if (filteredCount === 0 || filteredCount > 256) {
      throw new Error(
        `Expected 1..256 filtered rows, got ${filteredCount}`,
      );
    }

    const typeCells = await filteredRows.locator("td:nth-child(2)")
      .allTextContents();
    for (const type of typeCells) {
      if (!type.includes("Comunhão")) {
        throw new Error(`Expected a Comunhão result, got type cell "${type}"`);
      }
    }
  } finally {
    await browser.close();
  }
});
