/**
 * E2E test: quoted-phrase search on the song Content box
 *
 * Wrapping the `data=` value in double quotes switches stoma from token-AND to
 * a positional phrase match: the query tokens must appear as a contiguous,
 * in-order subsequence of the document's token stream. Per-token prefix still
 * applies, and every indexed token counts — including chord symbols — so a
 * chord line between two words breaks the phrase.
 *
 * Real-corpus assertions (user choice; corpus-dependent by design):
 * 1. `"minha alma tem sede"` -> exactly 1 row (sopra_em_nos). pra_te_adorar
 *    has the same four words but the chord line "Bb7 C Gm A#" between
 *    "A minha alma" and "Tem sede de ti" breaks contiguity, so it must NOT
 *    match. The box keeps the quoted value.
 * 2. reversed `"sede minha alma"` -> 0 rows (order matters).
 * 3. spread `"sede minha"` -> 0 rows (both tokens exist, never contiguous).
 * 4. unquoted `minha alma tem sede` -> 3 rows (token AND unchanged).
 * 5. `"tem sede de ti"` -> exactly 1 row (pra_te_adorar); sopra_em_nos also
 *    has all four tokens but the chord line "D A" between "sede" and "De Ti"
 *    breaks the phrase.
 *
 * Requires: axil running on :8080 (AUTH_SKIP_CONFIRM=1 for the full suite).
 */

import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";

async function totalRows(page: any): Promise<string | null> {
  const content = await page.content();
  const m = content.match(/(\d+) of (\d+) rows/);
  return m ? m[0] : null;
}

Deno.test("song content lookup: quoted phrase requires contiguous tokens", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await page.goto(`${BASE}/song/`, { waitUntil: "load" });
    await page.waitForSelector('input[name="data"].filter-lookup', {
      timeout: 5000,
    });

    const box = page.locator('input[name="data"].filter-lookup');
    const submit = page.locator('.hyle-filter-actions button[type="submit"]');

    // 1. Positive: contiguous in-order phrase -> exactly sopra_em_nos
    await box.fill('"minha alma tem sede"');
    await submit.click();
    await page.waitForURL(/data=%22minha/, { timeout: 10000 });

    const retained = await box.inputValue();
    if (retained !== '"minha alma tem sede"') {
      throw new Error(
        `Content box should keep the quoted phrase, got "${retained}"`,
      );
    }
    if ((await totalRows(page)) !== "1 of 1 rows") {
      throw new Error(
        `Expected exactly "1 of 1 rows" for "minha alma tem sede", got "${await totalRows(page)}"`,
      );
    }
    if (await page.locator('a[href="/song/sopra_em_nos"]').count() !== 1) {
      throw new Error(
        "sopra_em_nos must be the single quoted-phrase match",
      );
    }

    // 2. Reversed order -> 0 rows
    await box.fill('"sede minha alma"');
    await submit.click();
    await page.waitForURL(/data=%22sede/, { timeout: 10000 });
    if ((await totalRows(page)) !== "0 of 0 rows") {
      throw new Error(
        `Expected "0 of 0 rows" for reversed "sede minha alma", got "${await totalRows(page)}"`,
      );
    }

    // 3. Spread tokens -> 0 rows
    await box.fill('"sede minha"');
    await submit.click();
    await page.waitForURL(/data=%22sede/, { timeout: 10000 });
    if ((await totalRows(page)) !== "0 of 0 rows") {
      throw new Error(
        `Expected "0 of 0 rows" for spread "sede minha", got "${await totalRows(page)}"`,
      );
    }

    // 4. Unquoted AND on the same tokens -> 3 rows (contrast)
    await box.fill("minha alma tem sede");
    await submit.click();
    await page.waitForURL(/data=minha/, { timeout: 10000 });
    if ((await totalRows(page)) !== "3 of 3 rows") {
      throw new Error(
        `Expected "3 of 3 rows" for unquoted "minha alma tem sede", got "${await totalRows(page)}"`,
      );
    }
    for (const id of ["sopra_em_nos", "pra_te_adorar", "nao_podemos_caminhar"]) {
      if (await page.locator(`a[href="/song/${id}"]`).count() !== 1) {
        throw new Error(`unquoted AND must include ${id}`);
      }
    }

    // 5. Chord line breaks the phrase: sopra_em_nos must NOT match
    await box.fill('"tem sede de ti"');
    await submit.click();
    await page.waitForURL(/data=%22tem/, { timeout: 10000 });
    if ((await totalRows(page)) !== "1 of 1 rows") {
      throw new Error(
        `Expected exactly "1 of 1 rows" for "tem sede de ti", got "${await totalRows(page)}"`,
      );
    }
    if (await page.locator('a[href="/song/pra_te_adorar"]').count() !== 1) {
      throw new Error("pra_te_adorar must be the single match");
    }
    if (await page.locator('a[href="/song/sopra_em_nos"]').count() !== 0) {
      throw new Error(
        "sopra_em_nos must NOT match: the chord line 'D A' breaks 'tem sede de ti'",
      );
    }
  } finally {
    await browser.close();
  }
});
