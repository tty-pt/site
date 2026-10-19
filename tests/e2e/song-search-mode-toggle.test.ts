/**
 * E2E test: song list mode toggle
 *
 * Custom ↔ omni replace each other, drop the other mode's params,
 * persist across Apply and reload.
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
  name: "song mode toggle: custom ↔ omni",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      await page.goto(`${BASE}/song/`, { waitUntil: "load" });
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

      assert(
        await page.locator('[data-hyle-mode="omni"]').count() === 1,
        "default is omni mode",
      );
      assert(
        await page.locator('[data-hyle-omnisearch] input[name="q"]').count() ===
          1,
        "omni mode shows search box",
      );
      assert(
        await page.locator("details.hyle-multiselect").count() === 0,
        "omni mode hides field widgets",
      );
      assert(
        await page.locator('a[data-hyle-mode-toggle="custom"]').count() ===
          1,
        "toggle to fields present",
      );

      await page.locator('a[data-hyle-mode-toggle="custom"]').click();
      await waitFor(
        async () => {
          const u = new URL(page.url());
          return u.searchParams.get("custom") === "1";
        },
        10000,
        "toggle should set custom=1",
      );
      assert(
        await page.locator("details.hyle-multiselect").count() >= 1,
        "fields mode shows multiselect",
      );
      assert(
        await page.locator("[data-hyle-omnisearch]").count() === 0,
        "fields mode hides omnisearch",
      );

      await page.locator('a[data-hyle-mode-toggle="omni"]').click();
      await waitFor(
        async () => {
          const u = new URL(page.url());
          return u.searchParams.get("custom") !== "1";
        },
        10000,
        "toggle back to omni",
      );
      assert(
        await page.locator('[data-hyle-omnisearch] input[name="q"]').count() ===
          1,
        "omni input on after toggle",
      );
      assert(
        await page.locator("details.hyle-multiselect").count() === 0,
        "field widgets gone in omni",
      );

      await page.locator('[data-hyle-omnisearch] input[name="q"]').fill("Natal");
      await page.locator('[data-hyle-omnisearch] input[name="q"]').press("Enter");
      await waitFor(
        async () => {
          const u = new URL(page.url());
          return u.searchParams.get("custom") !== "1" &&
            (u.searchParams.get("q") ?? "") === "Natal";
        },
        10000,
        "Apply should keep omni and set q",
      );

      await page.locator('a[data-hyle-mode-toggle="custom"]').click();
      await waitFor(
        async () => {
          const u = new URL(page.url());
          return u.searchParams.get("custom") === "1" &&
            !u.searchParams.has("q");
        },
        10000,
        "toggle to fields should drop q",
      );
      assert(
        await page.locator("details.hyle-multiselect").count() >= 1,
        "field widgets back",
      );
      assert(
        await page.locator("[data-hyle-omnisearch]").count() === 0,
        "omnisearch gone after toggle back",
      );

      const details = page.locator(
        'details.hyle-multiselect[data-hyle-ms="type"]',
      );
      await details.locator("summary").click();
      const natalCb = page.locator(
        'details.hyle-multiselect input[name="type"][value="natal"]',
      );
      await natalCb.check();
      await page.locator('.hyle-filter-actions button[type="submit"]').click();
      await waitFor(
        async () => {
          const u = new URL(page.url());
          return u.searchParams.getAll("type").includes("natal");
        },
        10000,
        "type=natal after Apply",
      );
      await page.locator('a[data-hyle-mode-toggle="omni"]').click();
      await waitFor(
        async () => {
          const u = new URL(page.url());
          return u.searchParams.get("custom") !== "1" &&
            !u.searchParams.has("type");
        },
        10000,
        "toggle to omni should drop type=",
      );

      await page.goto(
        `${BASE}/song/?q=Natal`,
        { waitUntil: "load" },
      );
      assert(
        await page.locator('[data-hyle-mode="omni"]').count() === 1,
        "reload keeps omni",
      );
      const qInput = page.locator('[data-hyle-omnisearch] input[name="q"]');
      assert((await qInput.inputValue()) === "Natal", "reload keeps q value");

      await page.goto(
        `${BASE}/song/?custom=1&type=natal`,
        { waitUntil: "load" },
      );
      assert(
        await page.locator(
          'details.hyle-multiselect input[name="type"][value="natal"]',
        ).isChecked(),
        "reload keeps natal checked",
      );
    } finally {
      await browser.close();
    }
  },
});
