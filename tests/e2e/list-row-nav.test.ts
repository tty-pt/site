/**
 * E2E test: whole-row navigation on list pages (Part 2 row actions)
 *
 * Verifies that clicking a NON-link cell of a results row navigates to
 * the item page (stretched overlay anchor), on /song/ and /poem/.
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";

async function clickRowCellNavigates(
  page: import("npm:playwright").Page,
  module: string,
  useOverlay = false,
): Promise<boolean> {
  await page.goto(`${BASE}/${module}/`, { waitUntil: "domcontentloaded" });
  // Live locators only: element handles grabbed via page.$$ go stale
  // when bud's wasm hydration replaces the SSR tree mid-test
  // ("Cannot find context with specified id").
  const rows = page.locator("tr.hyle-row-clickable");
  if ((await rows.count()) === 0) return false; // empty module — skip

  const firstRow = rows.first();
  const cells = firstRow.locator("td");
  const nCells = await cells.count();
  if (nCells < 2) throw new Error(`${module}: row has <2 cells`);

  // Capture the row's target href BEFORE clicking.
  const href = await firstRow
    .locator("td:first-child a:not(.hyle-row-action)")
    .getAttribute("href");
  if (!href) throw new Error(`${module}: row link missing`);

  const targetPattern = href.replace(/\/$/, "");
  if (useOverlay) {
    await Promise.all([
      page.waitForURL((url) => url.href.includes(targetPattern), { timeout: 10000 }),
      firstRow.locator("a.hyle-row-action").first().click(),
    ]);
  } else {
    await Promise.all([
      page.waitForURL((url) => url.href.includes(targetPattern), { timeout: 10000 }),
      cells.nth(1).click({ force: true }),
    ]);
  }

  if (!page.url().includes(href.replace(/\/$/, ""))) {
    throw new Error(
      `${module}: clicked row cell, expected ${href}, got ${page.url()}`,
    );
  }
  return true;
}

Deno.test({
  name: "list rows: clicking a non-link cell navigates to the item",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const context = await browser.newContext();
  const page = await context.newPage();

  try {
    page.setDefaultNavigationTimeout(10000);

    const songOk = await clickRowCellNavigates(page, "song");
    if (!songOk) throw new Error("no song rows to test navigation on");

    // Prove generality once more on /poem/ when it has content
    const poemRows = await (async () => {
      await page.goto(`${BASE}/poem/`, { waitUntil: "domcontentloaded" });
      return await page.locator("tr.hyle-row-clickable").count();
    })();
    if (poemRows > 0) await clickRowCellNavigates(page, "poem");
  } finally {
    await browser.close();
  }
});

Deno.test({
  name: "list rows: no-JS row navigation still works",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const context = await browser.newContext({
    javaScriptEnabled: false,
  });
  const page = await context.newPage();

  try {
    page.setDefaultNavigationTimeout(10000);
    const ok = await clickRowCellNavigates(page, "song", true);
    if (!ok) throw new Error("no song rows to test no-JS navigation on");
  } finally {
    await browser.close();
  }
});
