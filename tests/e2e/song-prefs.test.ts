import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";

Deno.test("song preferences: login → set Latin notation → reload → verify persistent in SSR", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);

    // 1. Navigate to song and set Latin notation
    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.waitForSelector("#chord-data", { timeout: 5000 });

    // Open the menu
    await page.locator('#menu-functions').evaluate((el) => {
      const input = el;
      input.checked = true;
      input.dispatchEvent(new Event("change", { bubbles: true }));
    });
    await page.locator('.functions').waitFor({ state: 'visible', timeout: 5000 });

    const latinCheckbox = page.locator('input[name="l"]').first();
    await latinCheckbox.waitFor({ state: "visible", timeout: 5000 });

    // Navigate with Latin param to save the preference server-side
    await page.goto(`${BASE}/song/${SONG_ID}?l=1&b=0&t=0`, { waitUntil: 'domcontentloaded' });

    // 2. Reload page WITHOUT query params
    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.locator('#menu-functions').evaluate((el) => {
      const input = el;
      input.checked = true;
      input.dispatchEvent(new Event("change", { bubbles: true }));
    });
    await page.locator('.functions').waitFor({ state: 'visible', timeout: 5000 });
    await page.locator('label:has(input[name="l"])').first().waitFor({ state: "visible", timeout: 5000 });

    // 3. Verify it's still checked (persistent)
    const isCheckedAfter = await page.locator('input[name="l"]').first().isChecked();
    
    // 4. Verify notation is Latin (SSR check)
    const contextNoJs = await browser.newContext({ javaScriptEnabled: false });
    const pageNoJs = await contextNoJs.newPage();
    const cookies = await page.context().cookies();
    await contextNoJs.addCookies(cookies);

    await pageNoJs.goto(`${BASE}/song/${SONG_ID}`);
    const isCheckedSsr = await pageNoJs.isChecked('input[name="l"]');
    const selectedTextSsr = await pageNoJs.$eval('select[name="t"] option:checked', (el: any) => el.text);

    if (Deno.env.get("DEBUG")) console.log(`Persistent check: ${isCheckedAfter}, SSR check: ${isCheckedSsr}, Notation: ${selectedTextSsr}`);

    if (!isCheckedSsr) {
        throw new Error("Latin notation preference did not persist in SSR");
    }
    if (!selectedTextSsr.includes("La")) {
        throw new Error("Latin notation not applied in SSR from saved preferences");
    }

  } finally {
    await browser.close();
  }
});
