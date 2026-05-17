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
    
    // Open the menu
    await page.click('.menu-toggle');
    await page.waitForSelector('input[name="l"]', { timeout: 5000 });
    
    // Check if it's already checked (unlikely for new user)
    const isCheckedInitial = await page.isChecked('input[name="l"]');
    if (isCheckedInitial) {
        // Uncheck first to ensure we are testing the change
        await page.uncheck('input[name="l"]');
        await page.waitForTimeout(500); // Wait for CSR save
    }

    await page.check('input[name="l"]');
    await page.waitForTimeout(1000); // Wait for CSR save (debounce/async)

    // 2. Reload page WITHOUT query params
    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.click('.menu-toggle');
    await page.waitForSelector('input[name="l"]', { timeout: 5000 });

    // 3. Verify it's still checked (persistent)
    const isCheckedAfter = await page.isChecked('input[name="l"]');
    
    // 4. Verify notation is Latin (SSR check)
    // We disable JS for a moment to check SSR state
    const contextNoJs = await browser.newContext({ javaScriptEnabled: false });
    const pageNoJs = await contextNoJs.newPage();
    // We need to login again in this context or use the same cookies
    const cookies = await page.context().cookies();
    await contextNoJs.addCookies(cookies);

    await pageNoJs.goto(`${BASE}/song/${SONG_ID}`);
    // No need to "click" in NoJS, we can just check the checkbox state directly
    // but the checkbox might be hidden. isChecked works even if hidden if we don't need to click it.
    const isCheckedSsr = await pageNoJs.isChecked('input[name="l"]');
    const selectedTextSsr = await pageNoJs.$eval('select[name="t"] option:checked', (el: any) => el.text);

    console.log(`Persistent check: ${isCheckedAfter}, SSR check: ${isCheckedSsr}, Notation: ${selectedTextSsr}`);

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
