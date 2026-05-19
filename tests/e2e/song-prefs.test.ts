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

    const originalHtml = await page.innerHTML("#chord-data");
    if (!originalHtml) throw new Error("Chord data element was empty on load");

    // Open the menu
    await page.locator('#menu-functions').evaluate((el) => {
      const input = el;
      input.checked = true;
      input.dispatchEvent(new Event("change", { bubbles: true }));
    });
    await page.locator('.functions').waitFor({ state: 'visible', timeout: 5000 });

    const latinCheckbox = page.locator('input[name="l"]').first();
    await latinCheckbox.waitFor({ state: "visible", timeout: 5000 });

    const prefsPromise = page.waitForRequest(
      (req) => req.url().includes("/api/song/prefs") && req.method() === "POST",
      { timeout: 10000 },
    );

    await latinCheckbox.evaluate((el) => {
      const input = el;
      input.checked = true;
      input.dispatchEvent(new Event("input", { bubbles: true }));
    });

    // 1b. Verify chord display changed in real-time (before reload)
    const deadline = Date.now() + 5000;
    let chordChanged = false;
    while (Date.now() < deadline) {
      const chordHtml = await page.innerHTML("#chord-data");
      if (chordHtml !== originalHtml) {
        chordChanged = true;
        break;
      }
      await new Promise((r) => setTimeout(r, 100));
    }
    // Also wait for prefs persistence request
    await prefsPromise;

    if (!chordChanged) {
      throw new Error(
        "Chord display did not change in real-time after toggling Latin notation",
      );
    }

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
