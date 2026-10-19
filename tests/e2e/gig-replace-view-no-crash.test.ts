/**
 * E2E regression: GET /gig/:id?replace=N as owner must render the
 * inline replace picker, not kill the server.
 *
 * Bug: sb_load_song_picks passed the socket fd (int) as the char* body
 * argument of pick_view_collect_scoped -> immediate SIGSEGV whenever an
 * owner opened the replace view.
 */
import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const GOTO = { waitUntil: "domcontentloaded" as const };
const SONG_A_ID = "a_alegria_esta_no_coracao";
const SONG_A_TITLE = "A alegria está no coração";

Deno.test({
  name: "gig replace view (?replace=N) renders without crashing server",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    page.setDefaultNavigationTimeout(15000);
    page.setDefaultTimeout(15000);
    await createAndLoginUser(page, BASE);

    // grp + gig
    await page.goto(`${BASE}/grp/add`, GOTO);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', `ReplView Grp ${Date.now()}`);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/);
    const grpId = page.url().split("/grp/")[1];

    await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
    await page.fill('input[name="title"]', `ReplView SB ${Date.now()}`);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/);
    const sbId = page.url().split("/gig/")[1].replace(/\/$/, "");

    // seed one song
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");
    const { token, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);
    const r = await fetch(`${BASE}/api/gig/${sbId}/songs`, {
      method: "POST",
      body: new URLSearchParams({ song_id: SONG_A_ID, format: "any", csrf_token: token }).toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: ch },
      redirect: "manual",
    });
    if (r.status >= 400) throw new Error(`seed failed: ${r.status}`);
    await r.body?.cancel();

    // Owner opens the replace view for row 0 — server must survive
    // and show the inline Replace picker.
    const resp = await fetch(`${BASE}/gig/${sbId}?replace=0`, {
      headers: { Cookie: cookieHeader },
    });
    if (resp.status !== 200)
      throw new Error(`replace view expected 200, got ${resp.status}`);
    const html = await resp.text();
    if (!html.includes("Replace"))
      throw new Error("replace view missing Replace picker UI");

    // And the server must still be alive afterwards
    const health = await fetch(`${BASE}/`);
    if (health.status !== 200)
      throw new Error(`server unhealthy after replace view: ${health.status}`);
    await health.body?.cancel();

    // SSR page still shows the song list
    await page.goto(`${BASE}/gig/${sbId}`, GOTO);
    await waitForText(page, "body", SONG_A_TITLE);
  } finally {
    await browser.close();
  }
});
