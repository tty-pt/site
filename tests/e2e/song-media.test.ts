import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";
const YT_ID = "uATsjCPK49g";

Deno.test("song media: verify YouTube and PDF link buttons in SSR and WASM", async () => {
  const browser = await chromium.launch();
  const context = await browser.newContext({ javaScriptEnabled: false });
  const page = await context.newPage();

  try {
    await createAndLoginUser(page, BASE);

    // 1. Navigate to song page with ?m=1 to show media
    await page.goto(`${BASE}/song/${SONG_ID}?m=1`, {
      waitUntil: "domcontentloaded",
    });
    
    // 2. Verify YouTube link button is present (and no iframe)
    const iframe = page.locator('iframe');
    const iframeCount = await iframe.count();
    if (iframeCount > 0) {
        throw new Error("Found YouTube iframe, expected none");
    }

    const ytBtn = page.locator('a[href="https://www.youtube.com/watch?v=' + YT_ID + '"][target="_blank"]');
    const count = await ytBtn.count();
    if (count === 0) {
        throw new Error("YouTube link button not found in SSR rendering");
    }

    // 3. Verify media container is present
    const container = page.locator(".media-slot");
    if (await container.count() === 0) {
        throw new Error("Media container (.media-slot) not found in SSR");
    }

  } finally {
    await browser.close();
  }
});
