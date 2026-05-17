/**
 * E2E test: songbook edit — "Add Row" state loss bug reproduction
 *
 * Tests:
 *   1. Create a songbook
 *   2. Navigate to edit page
 *   3. Change the first row (song and key)
 *   4. Click "+ Add Row"
 *   5. Verify that the first row still has the changed values
 *
 * Currently, this test is EXPECTED TO FAIL because the SSR logic
 * loads from the dataset and ignores the POST body during "add_row" action.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const KNOWN_SONG_ID = "a_alegria_esta_no_coracao";
const KNOWN_SONG_TITLE = "A alegria está no coração";

Deno.test({
  name: "songbook edit: 'Add Row' should preserve existing form state",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let sbId: string | null = null;

  try {
    page.setDefaultNavigationTimeout(15000);
    page.setDefaultTimeout(15000);

    await createAndLoginUser(page, BASE);

    // 1. Create a songbook
    await page.goto(`${BASE}/songbook/add`);
    await page.fill('input[name="title"]', `Bug Repro SB ${Date.now()}`);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/songbook\/[^/]+$/);
    sbId = page.url().split("/songbook/")[1].replace(/\/$/, "");

    // 2. Go to edit page
    await page.goto(`${BASE}/songbook/${sbId}/edit`);
    await page.waitForSelector('input[name="song_0"]');

    // 3. Change row 0
    // We select a known song and a different key (e.g. D = 2)
    await page.fill('input[name="song_0"]', `${KNOWN_SONG_TITLE} [${KNOWN_SONG_ID}]`);
    await page.selectOption('select[name="key_0"]', { value: "2" });

    // 4. Click "+ Add Row"
    await page.click('button:has-text("+ Add Row")');

    // Wait for the page to reload
    // It should have song_1 now
    await page.waitForSelector('input[name="song_1"]', { timeout: 10000 });

    // 5. Verify row 0 state
    const songValue = await page.inputValue('input[name="song_0"]');
    const keyValue = await page.inputValue('select[name="key_0"]');

    console.log(`Row 0 after Add Row: song="${songValue}", key="${keyValue}"`);

    if (!songValue.includes(KNOWN_SONG_ID)) {
      throw new Error(`State Loss: song_0 was reset to "${songValue}"`);
    }
    if (keyValue !== "2") {
      throw new Error(`State Loss: key_0 was reset to "${keyValue}"`);
    }

  } finally {
    await browser.close();
  }
});
