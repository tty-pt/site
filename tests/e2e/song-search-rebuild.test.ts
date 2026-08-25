/**
 * E2E test: lazy full-text index rebuild on song edit
 *
 * Verifies the stoma index is rebuilt after a mutation through the live stack:
 * 1. add a song with a distinctive title -> searching it returns it
 * 2. edit the title to something else
 * 3. searching the OLD title returns 0 rows (stale token gone)
 * 4. searching the NEW title returns the song
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1 (auth helper).
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("song list: FTS index rebuilds after edit", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const oldTitle = "Zzz Search E2E";
  const newTitle = "Qqq Search E2E";
  const songId = "zzz_search_e2e";

  async function totalText(): Promise<string> {
    const content = await page.content();
    const m = content.match(/(\d+) of (\d+) rows/);
    if (m) return m[0];
    if (content.includes("No items")) return "0 of 0 rows";
    throw new Error(`Could not find "N of M rows" marker or "No items"`);
  }

  async function searchTitle(title: string): Promise<string> {
    await page.goto(`${BASE}/song/?custom=1&title=${encodeURIComponent(title)}`, {
      waitUntil: "load",
    });
    await page.waitForSelector("div.hyle-table-wrap, p.text-muted", { timeout: 10000 });
    return await totalText();
  }

  try {
    await createAndLoginUser(page, BASE);

    // ---- 1. Add a song with a distinctive title ----
    await page.goto(`${BASE}/song/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', oldTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(`${BASE}/song/${songId}`, { timeout: 5000 });

    // ---- 2. Search for the new song -> found ----
    const before = await searchTitle(oldTitle);
    if (before === "0 of 0 rows") {
      throw new Error(
        `Expected the just-added song for title="${oldTitle}", got "0 of 0 rows"`,
      );
    }

    // ---- 3. Edit the title ----
    await page.goto(`${BASE}/song/${songId}/edit`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', newTitle);
    await Promise.all([
      page.waitForURL(`${BASE}/song/${songId}`, { timeout: 10000 }),
      page.click('form[method="POST"] button[type="submit"]'),
    ]);

    // ---- 4. Old title no longer matches (stale token gone) ----
    const afterOld = await searchTitle(oldTitle);
    if (afterOld !== "0 of 0 rows") {
      throw new Error(
        `Expected "0 of 0 rows" for old title="${oldTitle}" after edit, got "${afterOld}"`,
      );
    }

    // ---- 5. New title matches ----
    const afterNew = await searchTitle(newTitle);
    if (afterNew === "0 of 0 rows") {
      throw new Error(
        `Expected the edited song for title="${newTitle}", got "0 of 0 rows"`,
      );
    }
  } finally {
    await browser.close();

    // ---- Cleanup: remove created song directory ----
    try {
      await Deno.remove(`var/song/${songId}`, { recursive: true });
    } catch {
      // Already gone or never created — ignore
    }
  }
});
