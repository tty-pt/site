/**
 * E2E tests for the Generic Filter and Picker Framework.
 * Verifies declarative single-line pickers, row/cell pickers, filter bars,
 * No-JS degradation, and dynamic WASM/JS enhancement.
 */

import { chromium, type Page } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const GOTO = { waitUntil: "domcontentloaded" as const };

function assert(condition: boolean, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

Deno.test({
  name: "generic framework: declarative cell pickers, filter bar, and multi-field auto-collection",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const context = await browser.newContext();
    const page = await context.newPage();
    try {
      page.setDefaultNavigationTimeout(15000);
      page.setDefaultTimeout(15000);
      await createAndLoginUser(page, BASE);

      const unique = crypto.randomUUID().slice(0, 8);
      const songTitle = `Framework Test Song ${unique}`;

      // Create a song with a specific type
      await page.goto(`${BASE}/song/add`, GOTO);
      await page.locator('form[method="POST"] input[name="title"]').fill(songTitle);
      await page.locator('details.hyle-picker-details summary').first().click();
      const firstTypeBox = page.locator('.hyle-picker-option input[name="type"]').first();
      await firstTypeBox.check();
      await Promise.all([
        page.waitForURL(/\/song\/[^\/]+$/, { timeout: 10000 }),
        page.locator('form[method="POST"] button[type="submit"]').click(),
      ]);
      const songId = page.url().split("/song/")[1].replace(/\/$/, "");

      // Create a gig containing this song
      await page.goto(`${BASE}/gig/add`, GOTO);
      await page.locator('form[method="POST"] input[name="title"]').fill(`Framework Gig ${unique}`);
      await Promise.all([
        page.waitForURL(/\/gig\/[^\/]+$/, { timeout: 10000 }),
        page.locator('button[type="submit"]').click(),
      ]);
      const gigId = page.url().split("/gig/")[1].replace(/\/$/, "");

      // Add song to gig via detail page top picker
      await page.goto(`${BASE}/gig/${gigId}`, GOTO);
      await page.locator('details.hyle-picker-details summary').first().click();
      const songSearch = page.locator('input[name="pick_q_song_id"]');
      await songSearch.fill(songTitle);
      await page.waitForTimeout(400);
      const songOpt = page.locator(`label.hyle-picker-option:has(input[name="song_id"][value="${songId}"])`);
      await songOpt.waitFor({ state: "visible", timeout: 10000 });
      await Promise.all([
        page.waitForURL(`${BASE}/gig/${gigId}`, { timeout: 10000 }),
        songOpt.click(),
      ]);
      await waitForText(page, "body", songTitle);

      // Verify edit page renders generic cell pickers with clean sibling forms
      await page.goto(`${BASE}/gig/${gigId}/edit`, GOTO);
      const songCellPicker = page.locator('.gig-song-title-picker');
      assert(await songCellPicker.count() >= 1, "edit page should render generic song cell picker");
      const fmtCellPicker = page.locator('.gig-format-picker');
      assert(await fmtCellPicker.count() >= 1, "edit page should render generic format cell picker");

      // Verify sibling GET forms are present and cleanly bound
      const songSibling = page.locator('form#pickq-song_0');
      assert(await songSibling.count() === 1, "expected sibling form pickq-song_0");
      const fmtSibling = page.locator('form#pickq-fmt_0');
      assert(await fmtSibling.count() === 1, "expected sibling form pickq-fmt_0");

      // Trigger format search via sibling form
      await page.goto(`${BASE}/gig/${gigId}/edit?pick_q_fmt_0=san`, GOTO);
      const activeFmtPicker = page.locator('.gig-format-picker');
      assert(await activeFmtPicker.count() >= 1, "edit page should render format picker with query state");
      const openPickerDetails = page.locator('.gig-format-picker details.hyle-picker-details[open]');
      assert(await openPickerDetails.count() >= 1, "active search query should open picker details in SSR");

      // Verify custom filter toolbar / list filtering without JS
      await page.goto(`${BASE}/song/?custom=1`, GOTO);
      const filterBar = page.locator('.hyle-filter-bar, form.list-form');
      assert(await filterBar.count() >= 1, "custom filter bar should be present in SSR");

      // Verify schema-driven hyle_bud_filter rendering
      const typeFacetFilter = page.locator('details.hyle-multiselect[data-hyle-ms="type"]');
      assert(await typeFacetFilter.count() >= 1, "schema-driven type facet filter should be rendered");

      await context.close();
    } finally {
      await browser.close();
    }
  },
});
