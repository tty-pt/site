/**
 * E2E test: song list omnisearch
 *
 * Covers default omni: one search box, no per-field widgets,
 * q matches title / author / type label, accent-sensitive, no-JS.
 *
 * Requires: axil running on :8080 (restarted with the new modules).
 */

import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";

function assert(cond: boolean, msg: string): void {
  if (!cond) throw new Error(msg);
}

async function waitFor(
  cond: () => Promise<boolean>,
  ms: number,
  label: string,
): Promise<void> {
  const deadline = Date.now() + ms;
  while (Date.now() < deadline) {
    if (await cond()) return;
    await new Promise((r) => setTimeout(r, 100));
  }
  throw new Error(`waitFor timeout: ${label}`);
}

Deno.test({
  name: "song omnisearch: SSR contract + apply + accents + labels",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      await page.goto(`${BASE}/song/`, { waitUntil: "load" });

      assert(
        await page.locator('[data-hyle-mode="omni"]').count() === 1,
        "expected data-hyle-mode=omni",
      );
      assert(
        await page.locator('[data-hyle-omnisearch] input[name="q"]').count() ===
          1,
        "expected one omnisearch input[name=q]",
      );
      assert(
        await page.locator("details.hyle-multiselect").count() === 0,
        "omni mode must not render multiselect",
      );
      assert(
        await page.locator('input[name="title"]').count() === 0,
        "omni mode must not render title filter",
      );
      assert(
        await page.locator('input[name="author"]').count() === 0,
        "omni mode must not render author filter",
      );
      assert(
        await page.locator('input[name="data"]').count() === 0,
        "omni mode must not render content lookup",
      );
      assert(
        await page.locator('input[name="custom"]').count() === 0,
        "omni must not emit hidden custom",
      );
      assert(
        await page.locator('input[name="mode"]').count() === 0,
        "omni must not emit hidden mode",
      );
      assert(
        await page.locator('a[data-hyle-mode-toggle="custom"]').count() ===
          1,
        "expected toggle to fields",
      );

      await page.locator('[data-hyle-omnisearch] input[name="q"]').fill(
        "Coração Adorador",
      );
      await page.locator('.hyle-filter-actions button[type="submit"]').click();
      await waitFor(
        async () => {
          const u = new URL(page.url());
          return !u.searchParams.has("custom") &&
            (u.searchParams.get("q") ?? "").includes("Cora");
        },
        10000,
        "URL should keep omni (no custom) and q",
      );
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
      const rows = page.locator("tr.hyle-row-clickable");
      assert(await rows.count() >= 1, "q=Coração Adorador should return ≥1 row");
      const titles = await rows.locator("td:first-child").allTextContents();
      assert(
        titles.join("|").includes("Coração Adorador"),
        "a visible title should contain Coração Adorador",
      );

      await page.locator('[data-hyle-omnisearch] input[name="q"]').fill(
        "coracao",
      );
      await page.locator('.hyle-filter-actions button[type="submit"]').click();
      await waitFor(
        async () => {
          const u = new URL(page.url());
          return (u.searchParams.get("q") ?? "") === "coracao";
        },
        10000,
        "URL should have q=coracao",
      );
      assert(
        (new URL(page.url()).searchParams.get("q") ?? "") === "coracao",
        "q=coracao stays in the URL",
      );

      await page.goto(
        `${BASE}/song/?q=${encodeURIComponent("Comunhão")}`,
        { waitUntil: "load" },
      );
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
      const labelRows = page.locator("tr.hyle-row-clickable");
      assert(await labelRows.count() >= 1, "q=Comunhão should return ≥1 row");
      const typeCells = await labelRows.locator("td:nth-child(2)")
        .allTextContents();
      assert(
        typeCells.some((t) => t.includes("Comunhão")),
        "a type cell should contain Comunhão",
      );
    } finally {
      await browser.close();
    }
  },
});

Deno.test({
  name: "song omnisearch: JS-disabled SSR still filters",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const context = await browser.newContext({ javaScriptEnabled: false });
    const page = await context.newPage();

    try {
      await page.goto(
        `${BASE}/song/?q=${encodeURIComponent("Coração")}`,
        { waitUntil: "load" },
      );
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
      assert(
        await page.locator('[data-hyle-omnisearch] input[name="q"]').count() ===
          1,
        "JS-off: omnisearch form present",
      );
      assert(
        await page.locator("tr.hyle-row-clickable").count() >= 1,
        "JS-off: q=Coração still filters",
      );
    } finally {
      await browser.close();
    }
  },
});
