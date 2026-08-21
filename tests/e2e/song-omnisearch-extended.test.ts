/**
 * E2E test: extended omnisearch coverage
 *
 * Gap coverage:
 *   1. omni scope — q= matches author field
 *   2. no-results empty state rendering
 *   3. omni + pagination — q= with >10 results paginates
 *   4. mode toggle — results change when switching custom ↔ omni
 *   5. delete + search — removed song vanishes from FTS
 *
 * Requires: axil running on :8080, AUTH_SKIP_CONFIRM=1 for test 5.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

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

function extractTotal(text: string): number {
  const m = text.match(/(\d+) of (\d+) rows/);
  if (m) return parseInt(m[2], 10);
  if (text.includes("No items")) return 0;
  throw new Error(`Could not find "N of M rows" or "No items"`);
}

/* ── 1. omni scope: q= matches author ────────────────────────── */

Deno.test({
  name: "omni scope: q= matches author field",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      // Joaquim dos Santos authored 2 songs
      await page.goto(
        `${BASE}/song/?q=${encodeURIComponent("Joaquim")}`,
        { waitUntil: "load" },
      );
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
      const rows1 = page.locator("tr.hyle-row-clickable");
      const count1 = await rows1.count();
      assert(count1 >= 2, `q=Joaquim should return ≥2 rows, got ${count1}`);

      const u1 = new URL(page.url());
      assert(!u1.searchParams.has("custom"), "should stay in omni mode");

      const authorCells1 = await rows1.locator("td:nth-child(3)")
        .allTextContents();
      assert(
        authorCells1.some((c) => c.includes("Joaquim")),
        "at least one author cell should contain Joaquim",
      );

      // Azevedo de Oliveira authored 2 songs
      await page.goto(
        `${BASE}/song/?q=${encodeURIComponent("Azevedo")}`,
        { waitUntil: "load" },
      );
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
      const rows2 = page.locator("tr.hyle-row-clickable");
      const count2 = await rows2.count();
      assert(count2 >= 2, `q=Azevedo should return ≥2 rows, got ${count2}`);

      const authorCells2 = await rows2.locator("td:nth-child(3)")
        .allTextContents();
      assert(
        authorCells2.some((c) => c.includes("Azevedo")),
        "at least one author cell should contain Azevedo",
      );
    } finally {
      await browser.close();
    }
  },
});

/* ── 2. no-results empty state ────────────────────────────────── */

Deno.test({
  name: "omni: no-results empty state",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      await page.goto(
        `${BASE}/song/?q=zzzzzznonexistent`,
        { waitUntil: "load" },
      );
      await page.waitForSelector("p.text-muted, tr.hyle-row-clickable", {
        timeout: 10000,
      });

      const content = await page.content();
      const rowCount = await page.locator("tr.hyle-row-clickable").count();
      assert(rowCount === 0, `expected 0 rows for garbage query, got ${rowCount}`);
      assert(
        content.includes("No items"),
        "empty state should show 'No items'",
      );

      // Empty query shows default list (not empty state)
      await page.goto(`${BASE}/song/?q=`, { waitUntil: "load" });
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
      const defaultRows = await page.locator("tr.hyle-row-clickable").count();
      assert(defaultRows > 0, "empty q should show default list");
    } finally {
      await browser.close();
    }
  },
});

/* ── 3. omni + pagination ─────────────────────────────────────── */

Deno.test({
  name: "omni + pagination: q= with >10 results paginates",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      // "Deus" appears in many song titles — should exceed 10 results
      await page.goto(
        `${BASE}/song/?q=${encodeURIComponent("Deus")}`,
        { waitUntil: "load" },
      );
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

      const rowsP1 = page.locator("tr.hyle-row-clickable");
      const countP1 = await rowsP1.count();
      assert(countP1 === 10, `page 1 should have 10 rows, got ${countP1}`);

      const total = extractTotal(await page.content());
      assert(total > 10, `total should be >10 for q=Deus, got ${total}`);

      const firstHrefP1 = await rowsP1.nth(0)
        .locator("td:first-child a:not(.hyle-row-action)")
        .getAttribute("href");

      // Click Next
      await page.locator('button:has-text("Next")').click();
      await page.waitForURL(/page=2/, { timeout: 10000 });
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

      const p2Content = await page.content();
      assert(p2Content.includes("Page 2"), "should show Page 2 indicator");

      const prevDisabled = await page.locator(
        'button:has-text("Prev")',
      ).isDisabled();
      assert(!prevDisabled, "Prev should be enabled on page 2");

      const rowsP2 = page.locator("tr.hyle-row-clickable");
      const countP2 = await rowsP2.count();
      assert(countP2 > 0, "page 2 should have rows");
      assert(countP2 <= 10, `page 2 should have ≤10 rows, got ${countP2}`);

      const firstHrefP2 = await rowsP2.nth(0)
        .locator("td:first-child a:not(.hyle-row-action)")
        .getAttribute("href");
      assert(
        firstHrefP2 !== firstHrefP1,
        "page 2 first result should differ from page 1",
      );
    } finally {
      await browser.close();
    }
  },
});

/* ── 4. mode toggle: results change ───────────────────────────── */

Deno.test({
  name: "mode toggle: results change when switching custom ↔ omni",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      // Start in custom mode with type=natal filter
      await page.goto(
        `${BASE}/song/?custom=1&type=natal`,
        { waitUntil: "load" },
      );
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

      assert(
        await page.locator('[data-hyle-mode="custom"]').count() === 1,
        "should be in custom mode",
      );

      // All visible rows should have Natal type
      const typesCustom = await page.locator("tr.hyle-row-clickable")
        .locator("td:nth-child(2)")
        .allTextContents();
      for (const t of typesCustom) {
        assert(
          t.includes("Natal"),
          `custom mode row should have Natal type, got "${t}"`,
        );
      }

      const firstHrefCustom = await page.locator("tr.hyle-row-clickable")
        .nth(0)
        .locator("td:first-child a:not(.hyle-row-action)")
        .getAttribute("href");

      // Toggle to omni — results should change (no longer restricted to natal)
      await page.locator('a[data-hyle-mode-toggle="omni"]').click();
      await waitFor(
        async () => {
          const u = new URL(page.url());
          return u.searchParams.get("custom") !== "1" && !u.searchParams.has("type");
        },
        10000,
        "toggle to omni should drop custom and type",
      );
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
      assert(
        await page.locator('[data-hyle-mode="omni"]').count() === 1,
        "should be in omni mode after toggle",
      );

      // Omni shows all songs (no type restriction) — more rows than natal-only
      const omniRows = await page.locator("tr.hyle-row-clickable").count();
      assert(omniRows >= 10, `omni should show ≥10 rows (full list), got ${omniRows}`);

      // First result in omni should differ from natal-filtered first result
      const firstHrefOmni = await page.locator("tr.hyle-row-clickable")
        .nth(0)
        .locator("td:first-child a:not(.hyle-row-action)")
        .getAttribute("href");
      assert(
        firstHrefOmni !== firstHrefCustom,
        "omni first result should differ from custom natal-filtered first result",
      );
    } finally {
      await browser.close();
    }
  },
});

/* ── 5. delete + search: removed song vanishes ────────────────── */

Deno.test({
  name: "delete + search: removed song vanishes from FTS",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();
    let songId: string | null = null;
    const songTitle = `Zzz Delete E2E ${Date.now()}`;

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      await createAndLoginUser(page, BASE);

      // Add a song
      await page.goto(`${BASE}/song/add`);
      await page.waitForSelector('input[name="title"]', { timeout: 5000 });
      await page.fill('input[name="title"]', songTitle);
      await page.click('button[type="submit"]');
      await page.waitForURL(/\/song\/[^/]+$/, { timeout: 10000 });
      songId = page.url().split("/song/")[1].replace(/\/$/, "");

      // Search via omni — should find it
      await page.goto(
        `${BASE}/song/?q=${encodeURIComponent(songTitle)}`,
        { waitUntil: "load" },
      );
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
      const rowsBefore = await page.locator("tr.hyle-row-clickable").count();
      assert(rowsBefore >= 1, "song should be findable after creation");

      // Delete via confirmation page
      await page.goto(`${BASE}/song/${songId}/delete`);
      await page.waitForSelector('button[type="submit"]', { timeout: 10000 });
      await page.click('button[type="submit"]');
      await page.waitForURL(`${BASE}/song`, { timeout: 10000 });

      // Search again — should find nothing
      await page.goto(
        `${BASE}/song/?q=${encodeURIComponent(songTitle)}`,
        { waitUntil: "load" },
      );
      await page.waitForSelector("p.text-muted, tr.hyle-row-clickable", {
        timeout: 10000,
      });
      const content = await page.content();
      assert(
        content.includes("No items") || extractTotal(content) === 0,
        "deleted song should not appear in search results",
      );
    } finally {
      await browser.close();
      if (songId) {
        try {
          await Deno.remove(`var/song/${songId}`, { recursive: true });
        } catch {
          /* already gone */
        }
      }
    }
  },
});
