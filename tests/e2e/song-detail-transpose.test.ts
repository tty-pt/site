/**
 * E2E test: song detail — live transposition via JS island.
 *
 * The SongView island makes async fetch calls to /api/song/:id/transpose
 * whenever the user changes the key/notation. This test verifies the island
 * correctly updates the chord display after selecting a different key.
 *
 * Requires: NDC (8080), Fresh (3000) running.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";

Deno.test("song detail: login → view song → transpose via island → verify display changed", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);

    // Navigate to the song detail page
    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.waitForSelector("#chord-data", { timeout: 5000 });

    // Grab the original chord display HTML
    const originalHtml = await page.innerHTML("#chord-data");
    if (!originalHtml) throw new Error("Chord data element was empty on load");

    // Verify the key selector exists and select a different key
    const selectEl = page.locator('select[name="t"]');

    // Select a different key (index 5 = +5 semitones)
    await selectEl.selectOption({ index: 5 });

    // Wait for the island to update the chord display by polling
    const deadline = Date.now() + 5000;
    let newHtml = "";
    while (Date.now() < deadline) {
      newHtml = await page.innerHTML("#chord-data");
      if (newHtml !== originalHtml) break;
      await new Promise((r) => setTimeout(r, 100));
    }

    // Verify the chord display changed
    if (newHtml === originalHtml) {
      throw new Error(
        "Chord display did not change after transpose.\n" +
          `Initial: ${originalHtml.slice(0, 100)}\n` +
          `After:   ${newHtml.slice(0, 100)}`,
      );
    }
  } finally {
    await browser.close();
  }
});
