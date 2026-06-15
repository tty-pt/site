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
    const GOTO = { waitUntil: "domcontentloaded" as const };

    // 0. Create a choir and seed the known song into repertoire
    const choirTitle = `SSR Test Choir ${Date.now()}`;
    await page.goto(`${BASE}/choir/add`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', choirTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/choir\/[^/]+$/, { timeout: 5000 });
    const choirId = page.url().split("/choir/")[1];

    const { token: csrfSeed, cookieHeader: chSeed } = await getCsrfToken(cookieHeader, BASE);
    const seedBody = new URLSearchParams({ song_id: SONG_ID, format: "any", csrf_token: csrfSeed });
    const seedResp = await fetch(`${BASE}/api/choir/${choirId}/songs`, {
      method: "POST",
      body: seedBody.toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chSeed },
      redirect: "manual",
    });
    if (seedResp.status >= 400) throw new Error(`Seed song failed: ${seedResp.status}`);
    await seedResp.body?.cancel();
    const repoId = `${choirId}_${SONG_ID}`;

    // 1. Create a songbook linked to the choir
    const sbTitle = `SSR Test SB ${Date.now()}`;
    await page.goto(`${BASE}/songbook/add?choir=${choirId}`, GOTO);
    await page.fill('input[name="title"]', sbTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/songbook\/[^/]+$/);
    sbId = page.url().split("/songbook/")[1].replace(/\/$/, "");

    // Add song via new API (url-encoded)
    const { token: csrfAdd, cookieHeader: chAdd } = await getCsrfToken(cookieHeader, BASE);
    const addBody = new URLSearchParams({ song_id: repoId, csrf_token: csrfAdd });
    const addResp = await fetch(`${BASE}/api/songbook/${sbId}/songs`, {
      method: "POST",
      body: addBody.toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chAdd },
      redirect: "manual",
    });
    if (addResp.status >= 400) throw new Error(`Add song to SB failed: ${addResp.status}`);
    await addResp.body?.cancel();

    // 2. Disable JS and verify SSR bolding
    const contextNoJs = await browser.newContext({ javaScriptEnabled: false });
    await contextNoJs.addCookies(cookies);
    const pageNoJs = await contextNoJs.newPage();
    
    await pageNoJs.goto(`${BASE}/songbook/${sbId}`);
    const chordHtml = await pageNoJs.innerHTML('[data-songbook-chord-data]');
    if (Deno.env.get("DEBUG")) console.log("Songbook Chord HTML snippet:", chordHtml.slice(0, 100));
    
    if (!chordHtml.includes("<b>")) {
        throw new Error("Chords are not bolded in Songbook SSR");
    }

    // 3. Check target key as guest (not logged in) with Latin notation via URL param
    const contextGuest = await browser.newContext({ javaScriptEnabled: false });
    const pageGuest = await contextGuest.newPage();
    await pageGuest.goto(`${BASE}/songbook/${sbId}?l=1`);
    const targetKeyText = await pageGuest.textContent('[data-songbook-target-key]');
    if (Deno.env.get("DEBUG")) console.log("Songbook Target Key (Latin?):", targetKeyText);
    
    // Original key of A alegria is A. In Latin it's La.
    if (!targetKeyText?.includes("La")) {
        throw new Error(`Expected Latin notation (La) in Songbook SSR, got: ${targetKeyText}`);
    }
    await contextGuest.close();

  } finally {
    await browser.close();
  }
});
