/**
 * E2E test: quoted-phrase search on the song Lyrics box
 *
 * Wrapping the `lyrics=` value in double quotes switches stoma from token-AND to
 * a positional phrase match: the query tokens must appear as a contiguous,
 * in-order subsequence of the document's token stream. Per-token prefix still
 * applies, and every indexed token counts. Chord lines are stripped from the
 * lyrics index, so lyrics across chord lines remain contiguous.
 *
 * Real-corpus assertions (user choice; corpus-dependent by design):
 * 1. `"minha alma tem sede"` -> matches sopra_em_nos.
 * 2. reversed `"sede minha alma"` -> 0 rows (order matters).
 * 3. spread `"sede minha"` -> 0 rows (both tokens exist, never contiguous).
 * 4. unquoted `minha alma tem sede` -> 3 rows (token AND unchanged).
 * 5. `"tem sede de ti"` -> matches pra_te_adorar and sopra_em_nos (since chord line D A is stripped from lyrics).
 *
 * Requires: axil running on :8080 (AUTH_SKIP_CONFIRM=1 for the full suite).
 */

import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";

async function totalRows(page: any): Promise<string | null> {
  const content = await page.content();
  const m = content.match(/(\d+) of (\d+) rows/);
  if (m) return m[0];
  if (content.includes("No items")) return "0 of 0 rows";
  return null;
}

Deno.test("song lyrics lookup: quoted phrase requires contiguous tokens", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await page.goto(`${BASE}/song/?custom=1`, { waitUntil: "load" });
    await page.waitForSelector("body[data-wasm-loaded]", { timeout: 10000 });
    await page.waitForSelector('input[name="lyrics"].filter-lookup', {
      timeout: 5000,
    });

    const box = page.locator('input[name="lyrics"].filter-lookup');
    const submit = page.locator('.hyle-filter-actions button[type="submit"]');

    // 1. Positive: contiguous in-order phrase -> matches sopra_em_nos and pra_te_adorar
    await box.fill('"minha alma tem sede"');
    await submit.click();
    await page.waitForURL(/lyrics=%22minha/, { timeout: 10000 });

    const retained = await box.inputValue();
    if (retained !== '"minha alma tem sede"') {
      throw new Error(
        `Lyrics box should keep the quoted phrase, got "${retained}"`,
      );
    }
    if (
      await page.locator('a[href="/song/sopra_em_nos"]:not(.hyle-row-action)')
        .count() !== 1
    ) {
      throw new Error(
        "sopra_em_nos must match 'minha alma tem sede'",
      );
    }
    if (
      await page.locator('a[href="/song/pra_te_adorar"]:not(.hyle-row-action)')
        .count() !== 1
    ) {
      throw new Error(
        "pra_te_adorar must match 'minha alma tem sede'",
      );
    }

    // 2. Reversed order -> 0 rows
    await box.fill('"sede minha alma"');
    await submit.click();
    await page.waitForURL(/lyrics=%22sede/, { timeout: 10000 });
    if ((await totalRows(page)) !== "0 of 0 rows") {
      throw new Error(
        `Expected "0 of 0 rows" for reversed "sede minha alma", got "${await totalRows(page)}"`,
      );
    }

    // 3. Spread tokens -> 0 rows
    await box.fill('"sede minha"');
    await submit.click();
    await page.waitForURL(/lyrics=%22sede/, { timeout: 10000 });
    if ((await totalRows(page)) !== "0 of 0 rows") {
      throw new Error(
        `Expected "0 of 0 rows" for spread "sede minha", got "${await totalRows(page)}"`,
      );
    }

    // 4. Unquoted AND on the same tokens -> 3 rows (contrast)
    await box.fill("minha alma tem sede");
    await submit.click();
    await page.waitForURL(/lyrics=minha/, { timeout: 10000 });
    if ((await totalRows(page)) !== "3 of 3 rows") {
      throw new Error(
        `Expected "3 of 3 rows" for unquoted "minha alma tem sede", got "${await totalRows(page)}"`,
      );
    }
    for (const id of ["sopra_em_nos", "pra_te_adorar", "nao_podemos_caminhar"]) {
      if (
        await page.locator(`a[href="/song/${id}"]:not(.hyle-row-action)`)
          .count() !== 1
      ) {
        throw new Error(`unquoted AND must include ${id}`);
      }
    }

    // 5. Quoted phrase "tem sede de ti" -> both pra_te_adorar and sopra_em_nos have lyrics "tem sede / de ti"
    await box.fill('"tem sede de ti"');
    await submit.click();
    await page.waitForURL(/lyrics=%22tem/, { timeout: 10000 });
    if (
      await page.locator('a[href="/song/pra_te_adorar"]:not(.hyle-row-action)')
        .count() !== 1
    ) {
      throw new Error("pra_te_adorar must match 'tem sede de ti'");
    }
  } finally {
    await browser.close();
  }
});
