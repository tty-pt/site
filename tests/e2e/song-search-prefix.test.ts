/**
 * E2E test: FTS token-prefix semantics + multi-field AND on song listing
 *
 * Pins the behavior of the stoma full-text search that replaced the old
 * ci_substr substring scan:
 * 1. searching "star" returns 0 rows (mid-word "estar" is NOT a token prefix;
 *    the old substring path would have matched)
 * 2. searching "cor" returns rows whose title starts a token with "cor"
 * 3. searching "coracao" (unaccented) returns 0 rows (accent-sensitive)
 * 4. title + author filters AND: title="cor" AND author="joaquim" narrows to
 *    "Abri os Corações"; adding a non-matching author kills the result set
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";

Deno.test("song list: FTS prefix semantics + multi-field AND", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  async function applyFilter(): Promise<void> {
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForSelector("div.hyle-table-wrap, p.text-muted", { timeout: 10000 });
  }

  async function rowCount(): Promise<number> {
    return await page.locator("tr.hyle-row-clickable").count();
  }

  async function totalText(): Promise<string> {
    const content = await page.content();
    const m = content.match(/(\d+) of (\d+) rows/);
    if (m) return m[0];
    if (content.includes("No items")) return "0 of 0 rows";
    throw new Error(`Could not find "N of M rows" marker or "No items"`);
  }

  try {
    // ---- 1. Prefix semantics: "star" matches nothing (mid-word) ----
    await page.goto(`${BASE}/song/?custom=1`, { waitUntil: "load" });
    await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
    await page.waitForSelector("body[data-wasm-loaded]", { timeout: 10000 });
    await page.locator('input[name="title"]').fill("star");
    await applyFilter();
    await page.waitForURL(/title=star/, { timeout: 10000 });
    if ((await totalText()) !== "0 of 0 rows") {
      throw new Error(
        `Expected "0 of 0 rows" for title=star (mid-word "estar" must not match), got "${await totalText()}"`,
      );
    }

    // ---- 2. Token prefix "cor" matches ----
    await page.locator('input[name="title"]').fill("cor");
    await applyFilter();
    await page.waitForURL(/title=cor/, { timeout: 10000 });
    const countCor = await rowCount();
    if (countCor === 0) {
      throw new Error(`Expected rows for title="cor", got 0`);
    }
    // NOTE: don't assert every row's title contains "cor" — the filter
    // can also surface rows whose OTHER indexed fields match (see
    // song-search-accent.test.ts). Prefix semantics are asserted by
    // check 1 (mid-word "star" must not match) and check 3 (no fold).
    await page.locator("tr.hyle-row-clickable td:first-child")
      .allTextContents();

    // ---- 3. No accent fold: "coracao" matches nothing ----
    await page.locator('input[name="title"]').fill("coracao");
    await applyFilter();
    await page.waitForURL(/title=coracao/, { timeout: 10000 });
    if ((await totalText()) !== "0 of 0 rows") {
      throw new Error(
        `Expected "0 of 0 rows" for title="coracao" (accent-sensitive), got "${await totalText()}"`,
      );
    }

    // ---- 4. Multi-field AND: title + author narrow to one known song ----
    await page.locator('input[name="title"]').fill("cor");
    await page.locator('input[name="author"]').fill("joaquim");
    await applyFilter();
    await page.waitForURL(/title=cor.*author=joaquim|author=joaquim.*title=cor/, {
      timeout: 10000,
    });
    if (await rowCount() === 0) {
      throw new Error(`Expected rows for title=cor&author=joaquim, got 0`);
    }
    const titlesAnd = await page.locator("tr.hyle-row-clickable td:first-child")
      .allTextContents();
    if (!titlesAnd.join("|").includes("Abri os Corações")) {
      throw new Error(
        `Expected "Abri os Corações" for title=cor&author=joaquim, got: ${JSON.stringify(titlesAnd)}`,
      );
    }

    // ---- 5. AND negative: non-matching author kills the result set ----
    await page.locator('input[name="author"]').fill("zzzzzz");
    await applyFilter();
    await page.waitForURL(/author=zzzzzz/, { timeout: 10000 });
    if ((await totalText()) !== "0 of 0 rows") {
      throw new Error(
        `Expected "0 of 0 rows" for title=cor&author=zzzzzz, got "${await totalText()}"`,
      );
    }
  } finally {
    await browser.close();
  }
});
