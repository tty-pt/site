/**
 * E2E test: auto-repertoire + list-grade song pickers (Part 2)
 *
 * Verifies:
 *   1. Repertoire is derived from gigs (auto-repertoire, Part 1)
 *   2. Grp picker default view = filter chrome only (no results table)
 *   3. Searching shows the results table; whole-row click adds the song
 *      and the page collapses to the plain detail URL
 *   4. Custom mode (?custom=1) exposes per-field widgets
 *   5. Pagination node renders while searching
 *   6. Gig picker: same row-click add; viewer pref hidden inputs exist
 *
 * Requires: axil running on :8080 (AUTH_SKIP_CONFIRM=1).
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test({
  name: "auto-repertoire: pickers derive + row-click add (grp & gig)",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await createAndLoginUser(page, BASE);
    const ts = Date.now();

    // ── 0. Create two songs ──────────────────────────────────────────
    const songA = `AutoRep Alpha ${ts}`;
    const songB = `AutoRep Beta ${ts}`;
    for (const title of [songA, songB]) {
      await page.goto(`${BASE}/song/add`);
      await page.waitForSelector('input[name="title"]');
      await page.fill('input[name="title"]', title);
      await page.click('button[type="submit"]');
      await page.waitForURL(/\/song\/[^/]+$/);
    }

    // ── 1. Create a grp ──────────────────────────────────────────────
    await page.goto(`${BASE}/grp/add`);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', `AutoRep Grp ${ts}`);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/);
    const grpId = page.url().split("/grp/")[1].replace(/\/$/, "");

    // ── 2. Picker default view: chrome only, no results table ────────
    await page.goto(`${BASE}/grp/${grpId}`);
    await page.waitForSelector(".hyle-filter-bar");
    let overlays = await page.$$("button.hyle-row-action");
    if (overlays.length !== 0) {
      throw new Error("picker must not render result rows by default");
    }

    // ── 3. Search → table + hint; ROW CLICK adds; collapse ───────────
    // Omni mode hides the Apply button (CSS), so submit via Enter.
    await page.fill('form.list-form input[name="q"]', songA);
    await page.press('form.list-form input[name="q"]', "Enter");
    await page.waitForSelector("button.hyle-row-action", { timeout: 8000 });
    await waitForText(page, "body", "Click a song to add it.");

    // Pagination node present while searching
    const pagText = await page.textContent(".hyle-table-footer");
    if (!pagText || !pagText.includes("Page 1")) {
      throw new Error(`pagination missing while searching: ${pagText}`);
    }

    await page.click("button.hyle-row-action >> nth=0");
    await page.waitForURL(new RegExp(`/grp/${grpId}$`), { timeout: 8000 });
    if (page.url().includes("?")) {
      throw new Error(`expected collapsed URL after add, got ${page.url()}`);
    }
    await waitForText(page, "body", songA);

    // ── 4. Custom mode exposes per-field widgets ─────────────────────
    await page.goto(`${BASE}/grp/${grpId}?custom=1`);
    await page.waitForSelector(".hyle-filter-bar[data-hyle-mode='custom']");
    const details = await page.$(".hyle-filter-bar details summary");
    if (details) await details.click();
    const typeBoxes = await page.$$(
      '.hyle-filter-bar input[type="checkbox"]',
    );
    if (typeBoxes.length === 0) {
      throw new Error("custom mode shows no per-field checkbox widgets");
    }

    // ── 5. Gig picker: row-click add + pref hidden inputs ────────────
    await page.goto(`${BASE}/gig/add?grp=${grpId}`);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', `AutoRep SB ${ts}`);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/);
    const sbId = page.url().split("/gig/")[1].replace(/\/$/, "");

    await page.goto(`${BASE}/gig/${sbId}`);
    await page.waitForSelector('form.list-form input[name="q"]');
    const hidT = await page.$('form.list-form input[name="t"][type="hidden"]');
    if (!hidT) throw new Error("gig picker missing t pref hidden input");

    await page.fill('form.list-form input[name="q"]', songB);
    await page.press('form.list-form input[name="q"]', "Enter");
    await page.waitForSelector("button.hyle-row-action", { timeout: 8000 });
    await page.click("button.hyle-row-action >> nth=0");
    await page.waitForURL(new RegExp(`/gig/${sbId}$`), { timeout: 8000 });
    await waitForText(page, "body", songB);

    // ── 6. Auto-repertoire: gig membership derives grp repertoire ────
    await page.goto(`${BASE}/grp/${grpId}`);
    await waitForText(page, "body", songB);
  } finally {
    await browser.close();
  }
});
