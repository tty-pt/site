/**
 * E2E test: song detail — live transposition via client-side JS.
 *
 * The song detail client-side enhancement makes async fetch calls to
 * /api/song/:id/transpose whenever the user changes the key/notation.
 * This test verifies the enhancement
 * correctly updates the chord display after selecting a different key.
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";

Deno.test("song detail: login → view song → toggle Latin notation → verify display changed", async () => {
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

    // Open the sidebar menu to access transpose controls
    await page.locator('#menu-functions').evaluate((el) => {
      const input = el;
      input.checked = true;
      input.dispatchEvent(new Event("change", { bubbles: true }));
    });
    await page.locator('.functions').waitFor({ state: 'visible', timeout: 5000 });

    // Toggle the Latin notation checkbox
    const latinCheckbox = page.locator('#transpose-form input[name="l"]');
    await latinCheckbox.waitFor({ state: "visible", timeout: 5000 });
    await latinCheckbox.evaluate((el) => {
      const input = el;
      input.checked = true;
      input.dispatchEvent(new Event("input", { bubbles: true }));
    });

    // Wait for the client-side update to change the chord display
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
        "Chord display did not change after toggling Latin notation.\n" +
          `Initial: ${originalHtml.slice(0, 100)}\n` +
          `After:   ${newHtml.slice(0, 100)}`,
      );
    }
  } finally {
    await browser.close();
  }
});

Deno.test("song detail: login → view song → transpose via JS → verify display changed", async () => {
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

    // Open the sidebar menu to access transpose controls
    await page.locator('#menu-functions').evaluate((el) => {
      const input = el;
      input.checked = true;
      input.dispatchEvent(new Event("change", { bubbles: true }));
    });
    await page.locator('.functions').waitFor({ state: 'visible', timeout: 5000 });

    // Select a different key via the sidebar transpose form
    const selectEl = page.locator('#transpose-form select[name="t"]');
    await selectEl.waitFor({ state: "visible", timeout: 5000 });
    await selectEl.selectOption({ index: 5 });

    // Wait for the client-side update to change the chord display
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
