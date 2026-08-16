/**
 * E2E test: song content lookup (lyric/chord `data` field)
 *
 * Pins the "Content" lookup box on /song that submits `?data=<text>` against
 * the stoma full-text index of each song's data.txt (VSTR field):
 * 1. seeding a song with multi-line lyrics then searching a unique token in
 *    the Content box narrows the list to that song and the box keeps its value
 * 2. token-prefix semantics ("xylo" matches "xylofrenia")
 * 3. multi-token AND ("xylofrenia" + "verse" both required)
 * 4. accent-sensitivity ("xylofreniacao" does NOT match "xylofreniação")
 * 5. a token beyond the (removed) 8KB fold-buffer cap is still indexed
 *
 * Quoted-phrase semantics live in tests/e2e/song-phrase-search.test.ts.
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

function uniqueId(): string {
  return `content_lookup_${Date.now()}`;
}

Deno.test("song content lookup: dedicated box filters by lyrics", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const songId = uniqueId();

  // >8KB lyric block: sentinel token sits past the old 8192-byte fold cap.
  const filler = "la la la\n".repeat(700); // 700*9 = 6300 bytes
  const longLyrics =
    `${filler}` +
    "Verse one xylofrenia\n" +
    "C major chord line\n" +
    "xylofreniação\n" +
    `xylofrenia verse two\n`.repeat(240) + // ~5600 more → total > 8192
    "deepdivezzz hidden at the far end\n";

  try {
    await createAndLoginUser(page, BASE);

    // ── 1. Seed a song with multi-line lyrics ──────────────────────────────
    await page.goto(`${BASE}/song/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', songId);
    await page.fill('textarea[name="data"]', longLyrics);
    await page.click('button[type="submit"]');
    await page.waitForURL(`${BASE}/song/${songId}`, { timeout: 5000 });

    // ── 2. Content box narrows the list; value is retained ────────────────
    await page.goto(`${BASE}/song/`, { waitUntil: "load" });
    await page.waitForSelector('input[name="data"].filter-lookup', {
      timeout: 5000,
    });
    await page.locator('input[name="data"].filter-lookup').fill("xylofrenia");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/data=xylofrenia/, { timeout: 10000 });

    const contentValue = await page
      .locator('input[name="data"].filter-lookup')
      .inputValue();
    if (contentValue !== "xylofrenia") {
      throw new Error(
        `Content box should keep "xylofrenia" after submit, got "${contentValue}"`,
      );
    }
    const rows = await page.locator("tr.hyle-row-clickable").count();
    if (rows < 1) {
      throw new Error(`Expected >= 1 row for data=xylofrenia, got 0`);
    }
    const visibleTitles = await page
      .locator("tr.hyle-row-clickable td:first-child")
      .allTextContents();
    if (!visibleTitles.some((t) => t.includes(songId))) {
      throw new Error(
        `Seeded song "${songId}" should be in data=xylofrenia results, got: ${JSON.stringify(visibleTitles)}`,
      );
    }

    // ── 3. Token-prefix semantics ──────────────────────────────────────────
    await page.locator('input[name="data"].filter-lookup').fill("xylo");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/data=xylo/, { timeout: 10000 });
    const rowCountXylo = await page.locator("tr.hyle-row-clickable").count();
    if (rowCountXylo < 1) {
      throw new Error(`Expected rows for data=xylo (prefix), got 0`);
    }

    // ── 4. Multi-token AND ─────────────────────────────────────────────────
    await page.locator('input[name="data"].filter-lookup').fill("xylofrenia verse");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/xylofrenia\+verse|verse.*xylofrenia/, {
      timeout: 10000,
    });
    if ((await page.locator("tr.hyle-row-clickable").count()) < 1) {
      throw new Error(`Expected rows for "xylofrenia verse" (AND), got 0`);
    }

    // AND negative: one bad token kills the set
    await page
      .locator('input[name="data"].filter-lookup')
      .fill("xylofrenia zapxq");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/zapxq/, { timeout: 10000 });
    const content = await page.content();
    const total = content.match(/(\d+) of (\d+) rows/);
    const isZero = (total && total[1] === "0") || content.includes("No items");
    if (!isZero) {
      throw new Error(
        `Expected "0 of ... rows" for "xylofrenia zapxq", got "${total?.[0]}"`,
      );
    }

    // ── 5. Accent-sensitive ────────────────────────────────────────────────
    // Unaccented "xylofreniacao" must NOT match the accented "xylofreniação"
    // (unique to the seeded song, so 0 rows proves accent-sensitivity).
    await page.locator('input[name="data"].filter-lookup').fill("xylofreniacao");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/xylofreniacao/, { timeout: 10000 });
    const contentAccent = await page.content();
    const totalAccent = contentAccent.match(/(\d+) of (\d+) rows/);
    const isZeroAccent = (totalAccent && totalAccent[1] === "0") || contentAccent.includes("No items");
    if (!isZeroAccent) {
      throw new Error(
        `Expected "0 of ... rows" for unaccented "xylofreniacao", got "${totalAccent?.[0]}"`,
      );
    }

    await page
      .locator('input[name="data"].filter-lookup')
      .fill("xylofreniação");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/xylofren/, { timeout: 10000 });
    if ((await page.locator("tr.hyle-row-clickable").count()) < 1) {
      throw new Error(
        `Expected rows for accented "xylofreniação" (prefix of "xylofreniação"), got 0`,
      );
    }

    // ── 6. >8KB value: far-end token still indexed (cap removed) ───────────
    await page
      .locator('input[name="data"].filter-lookup')
      .fill("deepdivezzz");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/deepdivezzz/, { timeout: 10000 });
    const rowsDeep = await page.locator("tr.hyle-row-clickable").count();
    if (rowsDeep < 1) {
      throw new Error(
        `Expected rows for "deepdivezzz" (token past 8KB must be indexed), got 0`,
      );
    }
  } finally {
    await browser.close();

    // ── Cleanup: remove created song directory ─────────────────────────────
    try {
      await Deno.remove(`items/song/items/${songId}`, { recursive: true });
    } catch {
      // Already gone or never created — ignore
    }
  }
});
