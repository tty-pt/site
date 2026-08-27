/**
 * E2E test: song lyrics lookup (`lyrics` field derived from data.txt)
 *
 * Pins the "Lyrics" lookup box on /song that submits `?lyrics=<text>` against
 * the stoma full-text index of each song's in-memory derived lyrics:
 * 1. seeding a song with chord lines and lyric lines
 * 2. searching a unique lyric token narrows the list to that song
 * 3. searching a unique chord token (e.g. "Cmaj7chordtest") returns 0 rows,
 *    verifying that chord lines are cleanly excluded from the lyrics index
 * 4. token-prefix semantics ("xylo" matches "xylofrenia")
 * 5. multi-token AND ("xylofrenia" + "verse" both required)
 * 6. accent-sensitivity ("xylofreniacao" does NOT match "xylofreniação")
 * 7. a token beyond the (removed) 8KB fold-buffer cap is still indexed
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

  // >8KB lyric block with distinct chord lines and lyric lines:
  const filler = "la la la\n".repeat(700); // 700*9 = 6300 bytes
  const chartWithChords =
    "C G Am F\n" +
    `${filler}` +
    "Verse one xylofrenia\n" +
    "Cmaj7 Dm7 G7sus4\n" +
    "xylofreniação\n" +
    "|: Em D/F# G :|\n" +
    `xylofrenia verse two\n`.repeat(240) + // ~5600 more → total > 8192
    "deepdivezzz hidden at the far end\n";

  try {
    await createAndLoginUser(page, BASE);

    // ── 1. Seed a song with chords and lyrics ──────────────────────────────
    await page.goto(`${BASE}/song/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', songId);
    await page.fill('textarea[name="data"]', chartWithChords);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(`${BASE}/song/${songId}`, { timeout: 5000 });

    // ── 2. Lyrics box narrows the list; value is retained ────────────────
    await page.goto(`${BASE}/song/?custom=1`, { waitUntil: "load" });
    await page.waitForSelector("body[data-wasm-loaded]", { timeout: 10000 });
    await page.waitForSelector('input[name="lyrics"].filter-lookup', {
      timeout: 5000,
    });
    await page.locator('input[name="lyrics"].filter-lookup').fill("xylofrenia");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/lyrics=xylofrenia/, { timeout: 10000 });

    const contentValue = await page
      .locator('input[name="lyrics"].filter-lookup')
      .inputValue();
    if (contentValue !== "xylofrenia") {
      throw new Error(
        `Lyrics box should keep "xylofrenia" after submit, got "${contentValue}"`,
      );
    }
    const rows = await page.locator("tr.hyle-row-clickable").count();
    if (rows < 1) {
      throw new Error(`Expected >= 1 row for lyrics=xylofrenia, got 0`);
    }
    const visibleTitles = await page
      .locator("tr.hyle-row-clickable td:first-child")
      .allTextContents();
    if (!visibleTitles.some((t) => t.includes(songId))) {
      throw new Error(
        `Seeded song "${songId}" should be in lyrics=xylofrenia results, got: ${JSON.stringify(visibleTitles)}`,
      );
    }

    // ── 3. Chords must NOT be indexed in lyrics ────────────────────────────
    await page.locator('input[name="lyrics"].filter-lookup').fill("G7sus4");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/lyrics=G7sus4/, { timeout: 10000 });
    const contentChord = await page.content();
    const totalChord = contentChord.match(/(\d+) of (\d+) rows/);
    const isZeroChord = (totalChord && totalChord[1] === "0") || contentChord.includes("No items");
    if (!isZeroChord) {
      throw new Error(
        `Expected "0 of ... rows" for chord token "G7sus4", got "${totalChord?.[0]}"`,
      );
    }

    // ── 4. Token-prefix semantics ──────────────────────────────────────────
    await page.locator('input[name="lyrics"].filter-lookup').fill("xylo");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/lyrics=xylo/, { timeout: 10000 });
    const rowCountXylo = await page.locator("tr.hyle-row-clickable").count();
    if (rowCountXylo < 1) {
      throw new Error(`Expected rows for lyrics=xylo (prefix), got 0`);
    }

    // ── 5. Multi-token AND ─────────────────────────────────────────────────
    await page.locator('input[name="lyrics"].filter-lookup').fill("xylofrenia verse");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/xylofrenia\+verse|verse.*xylofrenia/, {
      timeout: 10000,
    });
    if ((await page.locator("tr.hyle-row-clickable").count()) < 1) {
      throw new Error(`Expected rows for "xylofrenia verse" (AND), got 0`);
    }

    // AND negative: one bad token kills the set
    await page
      .locator('input[name="lyrics"].filter-lookup')
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

    // ── 6. Accent-sensitive ────────────────────────────────────────────────
    // Unaccented "xylofreniacao" must NOT match the accented "xylofreniação"
    // (unique to the seeded song, so 0 rows proves accent-sensitivity).
    await page.locator('input[name="lyrics"].filter-lookup').fill("xylofreniacao");
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
      .locator('input[name="lyrics"].filter-lookup')
      .fill("xylofreniação");
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForURL(/xylofren/, { timeout: 10000 });
    if ((await page.locator("tr.hyle-row-clickable").count()) < 1) {
      throw new Error(
        `Expected rows for accented "xylofreniação" (prefix of "xylofreniação"), got 0`,
      );
    }

    // ── 7. >8KB value: far-end token still indexed (cap removed) ───────────
    await page
      .locator('input[name="lyrics"].filter-lookup')
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
      await Deno.remove(`var/song/${songId}`, { recursive: true });
    } catch {
      // Already gone or never created — ignore
    }
  }
});
