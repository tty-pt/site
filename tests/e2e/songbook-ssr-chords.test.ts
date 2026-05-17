import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";
const SONG_TITLE = "A alegria está no coração";

Deno.test("songbook SSR: verify bolded chords and user prefs", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let sbId: string | null = null;

  try {
    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    // 1. Create a songbook with 1 song
    const sbTitle = `SSR Test SB ${Date.now()}`;
    await page.goto(`${BASE}/songbook/add`);
    await page.fill('input[name="title"]', sbTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/songbook\/[^/]+$/);
    sbId = page.url().split("/songbook/")[1].replace(/\/$/, "");

    // Add song via edit page
    const { token: csrfAdd } = await getCsrfToken(cookieHeader, BASE);
    const fd = new FormData();
    fd.append("amount", "1");
    fd.append("song_0", `${SONG_TITLE} [${SONG_ID}]`);
    fd.append("key_0", "0");
    fd.append("orig_0", "0");
    fd.append("fmt_0", "any");
    fd.append("csrf_token", csrfAdd);

    await fetch(`${BASE}/songbook/${sbId}/edit`, {
      method: "POST",
      body: fd,
      headers: { Cookie: cookieHeader },
    });

    // 2. Disable JS and verify SSR bolding
    const contextNoJs = await browser.newContext({ javaScriptEnabled: false });
    await contextNoJs.addCookies(cookies);
    const pageNoJs = await contextNoJs.newPage();
    
    await pageNoJs.goto(`${BASE}/songbook/${sbId}`);
    const chordHtml = await pageNoJs.innerHTML('[data-songbook-chord-data]');
    console.log("Songbook Chord HTML snippet:", chordHtml.slice(0, 100));
    
    if (!chordHtml.includes("<b>")) {
        throw new Error("Chords are not bolded in Songbook SSR");
    }

    // 3. Set Latin preference and verify it's applied in Songbook SSR
    await page.goto(`${BASE}/song/${SONG_ID}`); // Go to any song to set global prefs
    await page.click('.menu-toggle');
    await page.check('input[name="l"]');
    await page.waitForTimeout(1000);

    await pageNoJs.goto(`${BASE}/songbook/${sbId}`);
    const targetKeyText = await pageNoJs.textContent('[data-songbook-target-key]');
    console.log("Songbook Target Key (Latin?):", targetKeyText);
    
    // Original key of A alegria is A. In Latin it's La.
    if (!targetKeyText?.includes("La")) {
        throw new Error(`Expected Latin notation (La) in Songbook SSR, got: ${targetKeyText}`);
    }

  } finally {
    await browser.close();
  }
});
