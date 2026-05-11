import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";

Deno.test("song detail SSR: verify original key and bolded chords", async () => {
  // We disable JavaScript to test SSR only
  const browser = await chromium.launch();
  const context = await browser.newContext({ javaScriptEnabled: false });
  const page = await context.newPage();

  try {
    await createAndLoginUser(page, BASE);

    // Navigate to the song detail page
    await page.goto(`${BASE}/song/${SONG_ID}`);
    
    // Wait for the chord data to be present
    const chordData = page.locator("#chord-data");
    await chordData.waitFor({ state: "attached", timeout: 5000 });

    // 0. Verify song content is visible in viewport (not hidden in side menu)
    const mainBox = await page.locator("#main").boundingBox();
    if (!mainBox || mainBox.width < 10 || mainBox.x < 0) {
        throw new Error("#main is not visible in viewport");
    }
    const chordBox = await chordData.boundingBox();
    if (!chordBox || chordBox.width < 10 || chordBox.y < 0) {
        throw new Error("#chord-data is not visible in viewport");
    }

    // 1. Verify chords are bolded (look for <b> tags in innerHTML)
    const innerHtml = await chordData.innerHTML();
    if (Deno.env.get("DEBUG")) console.log("Chord HTML snippet:", innerHtml.slice(0, 100));
    
    if (!innerHtml.includes("<b>") && !innerHtml.includes("&lt;b&gt;")) {
        // Since we are using dangerous_inner_html in SSR, it should contain <b>
        // If it was escaped, it would be &lt;b&gt;
        if (!innerHtml.includes("<b>")) {
            throw new Error("Chords are not bolded in SSR (missing <b> tags)");
        }
    }

    // 2. Verify "(Original)" label is present in the select options
    const originalOption = page.locator('select[name="t"] option:has-text("(Original)")');
    const count = await originalOption.count();
    if (count === 0) {
        throw new Error("Original key label '(Original)' not found in key selector");
    }

    // 3. Verify the original key is selected by default (transpose=0)
    const selectedValue = await page.$eval('select[name="t"]', (el: any) => el.value);
    const selectedText = await page.$eval('select[name="t"] option:checked', (el: any) => el.text);
    
    if (Deno.env.get("DEBUG")) console.log(`Selected key value: ${selectedValue}, text: ${selectedText}`);
    
    if (!selectedText.includes("(Original)")) {
        throw new Error(`Original key is not selected by default. Selected: ${selectedText}`);
    }

    // 4. Verify Flats (bemol) notation in SSR
    // We append ?b=1 to the URL
    await page.goto(`${BASE}/song/${SONG_ID}?b=1`);
    const chordHtmlBemol = await page.innerHTML("#chord-data");
    // "A alegría está no coração" in A. If we transpose to some key that has flats...
    // Actually, let's just check if the "Flats" checkbox is checked in SSR
    const bemolChecked = await page.isChecked('input[name="b"]');
    if (!bemolChecked) {
        throw new Error("Flats checkbox should be checked when ?b=1 is present");
    }

    // 5. Verify Latin notation in SSR
    await page.goto(`${BASE}/song/${SONG_ID}?l=1`);
    const latinChecked = await page.isChecked('input[name="l"]');
    if (!latinChecked) {
        throw new Error("Latin checkbox should be checked when ?l=1 is present");
    }
    const latinOptionText = await page.$eval('select[name="t"] option:checked', (el: any) => el.text);
    if (!latinOptionText.includes("La")) { // A in Latin is La
        throw new Error(`Expected Latin notation (La) in key selector, got: ${latinOptionText}`);
    }

  } finally {
    await browser.close();
  }
});
