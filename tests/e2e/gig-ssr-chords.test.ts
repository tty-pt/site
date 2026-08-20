import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";
const SONG_TITLE = "A alegria está no coração";

Deno.test("gig SSR: verify bolded chords and user prefs", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let sbId: string | null = null;

  try {
    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");
    const GOTO = { waitUntil: "domcontentloaded" as const };

    // 0. Create a grp and seed the known song into repertoire
    const grpTitle = `SSR Test Grp ${Date.now()}`;
    await page.goto(`${BASE}/grp/add`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', grpTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/, { timeout: 5000 });
    const grpId = page.url().split("/grp/")[1];

    const { token: csrfSeed, cookieHeader: chSeed } = await getCsrfToken(cookieHeader, BASE);
    const seedBody = new URLSearchParams({ song_id: SONG_ID, format: "any", csrf_token: csrfSeed });
    const seedResp = await fetch(`${BASE}/api/grp/${grpId}/songs`, {
      method: "POST",
      body: seedBody.toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chSeed },
      redirect: "manual",
    });
    if (seedResp.status >= 400) throw new Error(`Seed song failed: ${seedResp.status}`);
    await seedResp.body?.cancel();
    const repoId = `${SONG_ID}`;

    // 1. Create a gig linked to the grp
    const sbTitle = `SSR Test SB ${Date.now()}`;
    await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
    await page.fill('input[name="title"]', sbTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/);
    sbId = page.url().split("/gig/")[1].replace(/\/$/, "");

    // Add song via new API (url-encoded)
    const { token: csrfAdd, cookieHeader: chAdd } = await getCsrfToken(cookieHeader, BASE);
    const addBody = new URLSearchParams({ song_id: repoId, csrf_token: csrfAdd });
    const addResp = await fetch(`${BASE}/api/gig/${sbId}/songs`, {
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
    
    await pageNoJs.goto(`${BASE}/gig/${sbId}`);
    const chordHtml = await pageNoJs.innerHTML('[data-gig-chord-data]');
    if (Deno.env.get("DEBUG")) console.log("Gig Chord HTML snippet:", chordHtml.slice(0, 100));
    
    if (!chordHtml.includes("<b>")) {
        throw new Error("Chords are not bolded in Gig SSR");
    }

    // 3. Check target key as guest (not logged in) with Latin notation via URL param
    const contextGuest = await browser.newContext({ javaScriptEnabled: false });
    const pageGuest = await contextGuest.newPage();
    await pageGuest.goto(`${BASE}/gig/${sbId}?l=1`);
    const targetKeyText = await pageGuest.textContent('[data-gig-target-key]');
    if (Deno.env.get("DEBUG")) console.log("Gig Target Key (Latin?):", targetKeyText);
    
    // Original key of A alegria is A. In Latin it's La.
    if (!targetKeyText?.includes("La")) {
        throw new Error(`Expected Latin notation (La) in Gig SSR, got: ${targetKeyText}`);
    }
    await contextGuest.close();

  } finally {
    await browser.close();
  }
});
