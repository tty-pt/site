import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";
const YT_ID = "uATsjCPK49g";

Deno.test("song media: verify YouTube iframe in SSR", async () => {
  const browser = await chromium.launch();
  // Disable JS to ensure we test SSR rendering of media
  const context = await browser.newContext({ javaScriptEnabled: false });
  const page = await context.newPage();

  try {
    await createAndLoginUser(page, BASE);

    // 1. Navigate to song page with ?m=1 to show media
    await page.goto(`${BASE}/song/${SONG_ID}?m=1`);
    
    // 2. Verify YouTube iframe is present
    const iframe = page.locator('iframe[src*="' + YT_ID + '"]');
    const count = await iframe.count();
    
    if (count === 0) {
        throw new Error("YouTube iframe not found in SSR rendering");
    }

    // 3. Verify media container is present
    const container = page.locator("#media-container");
    if (await container.count() === 0) {
        throw new Error("Media container (#media-container) not found in SSR");
    }

  } finally {
    await browser.close();
  }
});
