/**
 * E2E test: song type multi-select dropdown filter
 *
 * Covers the SSR-first, WASM-enhanced multiselect on the song listing:
 *   1. SSR contract: <details class="hyle-multiselect" data-hyle-ms="type">,
 *      real checkboxes (no-JS baseline), search input, options container,
 *      id="bud-state", data-modules="list".
 *   2. WASM hydration: body[data-wasm-loaded] appears (bundle mounted).
 *   3. Live option search: typing filters the option rows (hyle-ms-hidden).
 *   4. Summary label sync: checking options updates the trigger text
 *      (semicolon-joined display labels).
 *   5. Apply -> repeated-key union URL (?type=a&type=b), filtered table.
 *   6. Reload keeps checked state and summary.
 *   7. JS-disabled context: same union filter works via native form submit
 *      and SSR (no enhancement required).
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
  name: "song type: multiselect dropdown SSR + WASM enhancement",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      await page.goto(`${BASE}/song/?custom=1`, { waitUntil: "load" });
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

      // ---- 1. SSR contract (works with no JS at all) ----
      const details = page.locator(
        'details.hyle-multiselect[data-hyle-ms="type"]',
      );
      assert(
        await details.count() === 1,
        "expected one details.hyle-multiselect[data-hyle-ms=type]",
      );

      const searchInput = page.locator(
        'details.hyle-multiselect input[data-hyle-ms-search]',
      );
      assert(
        await searchInput.count() === 1,
        "expected a data-hyle-ms-search input",
      );

      const optionsBox = page.locator(
        'details.hyle-multiselect [data-hyle-ms-options]',
      );
      assert(
        await optionsBox.count() === 1,
        "expected a data-hyle-ms-options container",
      );

      const natalCb = page.locator(
        'details.hyle-multiselect input[name="type"][value="natal"]',
      );
      const comunhaoCb = page.locator(
        'details.hyle-multiselect input[name="type"][value="comunhao"]',
      );
      assert(
        await natalCb.count() === 1 && await comunhaoCb.count() === 1,
        "expected real natal/comunhao type checkboxes (no-JS baseline)",
      );

      const stateScript = page.locator('script#bud-state');
      assert(
        await stateScript.count() === 1,
        "expected <script id=bud-state> with the JSON state",
      );
      const stateJson = await stateScript.textContent();
      assert(
        !!stateJson && stateJson.includes('"module":"song"'),
        "bud-state should be the song list state JSON",
      );

      assert(
        await page.locator("body[data-modules]").count() === 1,
        "expected data-modules on body",
      );

      // ---- 2. WASM hydration ----
      await page.waitForSelector("body[data-wasm-loaded]", { timeout: 10000 });

      // ---- 3. Live option search ----
      await details.locator("summary").click();
      await searchInput.fill("comun");
      const allOptions = page.locator(
        'details.hyle-multiselect .hyle-ms-option',
      );
      const hiddenOptions = page.locator(
        'details.hyle-multiselect .hyle-ms-option.hyle-ms-hidden',
      );
      const comunhaoOpt = page.locator(
        'details.hyle-multiselect .hyle-ms-option:has(input[value="comunhao"])',
      );
      await waitFor(
        async () => {
          const total = await allOptions.count();
          const hidden = await hiddenOptions.count();
          const comunhaoHidden = await comunhaoOpt.evaluate(
            (el) => el.classList.contains("hyle-ms-hidden"),
          );
          return total > 0 && hidden > 0 && !comunhaoHidden;
        },
        5000,
        "live search should leave only Comunhão visible",
      );

      // ---- 4. Summary label sync (WASM patch) ----
      await searchInput.fill("");
      await natalCb.check();
      const saidaCb = page.locator(
        'details.hyle-multiselect input[name="type"][value="saida"]',
      );
      await saidaCb.check();
      const summary = page.locator(
        'details.hyle-multiselect [data-hyle-ms-values]',
      );
      await waitFor(
        async () => {
          const t = (await summary.textContent()) ?? "";
          return t.includes("Natal") && t.includes("Saída");
        },
        5000,
        "summary should sync to both selections",
      );
      const summaryText = (await summary.textContent())?.trim() ?? "";

      // ---- 5. Apply -> AND filter URL (type default = AND) ----
      await page.locator('.hyle-filter-actions button[type="submit"]').click();
      await waitFor(
        async () => {
          const u = new URL(page.url());
          const types = u.searchParams.getAll("type");
          return types.includes("natal") && types.includes("saida");
        },
        10000,
        "URL should carry type=natal and type=saida",
      );
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

      const content = await page.content();
      assert(
        content.includes("type=natal") && content.includes("type=saida"),
        "URL should carry both type=natal and type=saida",
      );

      const rows = page.locator("tr.hyle-row-clickable");
      const n = await rows.count();
      assert(n > 0, "AND filter (natal+saida) should return rows");
      const typeCells = await rows.locator("td:nth-child(2)").allTextContents();
      for (const t of typeCells) {
        assert(
          t.includes("Natal") && t.includes("Saída"),
          `AND-filtered row should contain BOTH Natal and Saída, got: "${t}"`,
        );
      }

      // ---- 6. Reload keeps checked state + summary ----
      await page.reload({ waitUntil: "load" });
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
      assert(
        await page.locator(
          'details.hyle-multiselect input[name="type"][value="natal"]',
        ).isChecked(),
        "natal checkbox should stay checked after reload",
      );
      assert(
        await page.locator(
          'details.hyle-multiselect input[name="type"][value="saida"]',
        ).isChecked(),
        "saida checkbox should stay checked after reload",
      );
      const reloadSummary = (await page.locator(
        'details.hyle-multiselect [data-hyle-ms-values]',
      ).textContent())?.trim() ?? "";
      assert(
        reloadSummary.includes("Natal") && reloadSummary.includes("Saída"),
        `reload summary should list both selections, got: "${reloadSummary}"`,
      );
    } finally {
      await browser.close();
    }
  },
});

Deno.test({
  name: "song type: AND filter and OR override work with JS disabled (SSR only)",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const context = await browser.newContext({ javaScriptEnabled: false });
    const page = await context.newPage();

    try {
      // AND default: natal+saida → only dual-typed songs
      await page.goto(
        `${BASE}/song/?custom=1&type=natal&type=saida`,
        { waitUntil: "load" },
      );
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

      const rows = page.locator("tr.hyle-row-clickable");
      const n = await rows.count();
      assert(n > 0, "JS-disabled AND filter (natal+saida) should return rows");
      const typeCells = await rows.locator("td:nth-child(2)").allTextContents();
      for (const t of typeCells) {
        assert(
          t.includes("Natal") && t.includes("Saída"),
          `JS-disabled AND-filtered row should contain BOTH Natal and Saída, got: "${t}"`,
        );
      }

      assert(
        await page.locator(
          'details.hyle-multiselect input[name="type"][value="natal"]',
        ).isChecked(),
        "JS-disabled: natal checkbox should render checked from SSR",
      );
      assert(
        await page.locator(
          'details.hyle-multiselect input[name="type"][value="saida"]',
        ).isChecked(),
        "JS-disabled: saida checkbox should render checked from SSR",
      );

      // OR override: natal+comunhao with type_op=or → union
      await page.goto(
        `${BASE}/song/?custom=1&type=natal&type=comunhao&type_op=or`,
        { waitUntil: "load" },
      );
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

      const orRows = page.locator("tr.hyle-row-clickable");
      const orN = await orRows.count();
      assert(orN > 0, "JS-disabled OR override (natal+comunhao) should return rows");
      const orTypeCells = await orRows.locator("td:nth-child(2)").allTextContents();
      for (const t of orTypeCells) {
        assert(
          t.includes("Natal") || t.includes("Comunhão"),
          `JS-disabled OR-filtered row should be Natal or Comunhão, got: "${t}"`,
        );
      }
    } finally {
      await browser.close();
    }
  },
});
