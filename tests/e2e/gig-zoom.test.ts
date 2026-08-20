/**
 * E2E test: gig zoom slider
 *
 * Tests:
 *   1. Create gig with a song via the new add API
 *   2. View gig page, verify zoom slider exists
 *   3. Change zoom via slider, verify data-zoom attribute
 *   4. Reload and verify zoom persists in SSR
 *   5. Cross-module: gig → song page (l/m/z via GET /api/song/prefs)
 *   6. Cross-module: song page → gig (transpose endpoint persists)
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";

Deno.test({
  name: "gig zoom: slider changes chord-zoom and persists",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let sbId: string | null = null;

  // Capture browser console for WASM diagnostics
  const browserLogs: string[] = [];
  page.on("console", (msg) => {
    browserLogs.push(`[${msg.type()}] ${msg.text()}`);
  });
  page.on("pageerror", (err) => {
    browserLogs.push(`[PAGE_ERROR] ${err.message}`);
  });

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");
    const GOTO = { waitUntil: "domcontentloaded" as const };

    // ── 0. Create a grp and seed the song ───────────────────────────────
    const grpTitle = `Zoom Grp ${Date.now()}`;
    await page.goto(`${BASE}/grp/add`, GOTO);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', grpTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/);
    const grpId = page.url().split("/grp/")[1];

    const { token: csrfSeed, cookieHeader: chSeed } =
      await getCsrfToken(cookieHeader, BASE);
    const seedBody = new URLSearchParams({
      song_id: SONG_ID,
      format: "any",
      csrf_token: csrfSeed,
    });
    const seedResp = await fetch(`${BASE}/api/grp/${grpId}/songs`, {
      method: "POST",
      body: seedBody.toString(),
      headers: {
        "Content-Type": "application/x-www-form-urlencoded",
        Cookie: chSeed,
      },
      redirect: "manual",
    });
    if (seedResp.status >= 400)
      throw new Error(`Seed song failed: ${seedResp.status}`);
    await seedResp.body?.cancel();
    const repoId = `${SONG_ID}`;

    // ── 1. Create a gig linked to the grp ──────────────────────────
    const sbTitle = `Zoom Test SB ${Date.now()}`;
    await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', sbTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/);
    sbId = page.url().split("/gig/")[1].replace(/\/$/, "");

    // ── 2. Add song via new API (url-encoded) ─────────────────────────────
    const { token: csrfAdd, cookieHeader: chAdd } =
      await getCsrfToken(cookieHeader, BASE);
    const addBody = new URLSearchParams({
      song_id: repoId,
      csrf_token: csrfAdd,
    });
    const addResp = await fetch(`${BASE}/api/gig/${sbId}/songs`, {
      method: "POST",
      body: addBody.toString(),
      headers: {
        "Content-Type": "application/x-www-form-urlencoded",
        Cookie: chAdd,
      },
      redirect: "manual",
    });
    if (addResp.status >= 400)
      throw new Error(`Add song to SB failed: ${addResp.status}`);
    await addResp.body?.cancel();

    // ── 3. View gig and verify slider exists ─────────────────────────
    await page.goto(`${BASE}/gig/${sbId}`, GOTO);
    await page.waitForSelector("#sb-main");

    const initialZoom = await page.getAttribute("#sb-main", "data-zoom");
    if (Deno.env.get("DEBUG"))
      console.log(`Initial zoom: ${initialZoom}`);

    // The viewer-controls section is not inside the functions popup;
    // it's rendered directly in the layout. Find the slider.
    const slider = page.locator(
      'input[type="range"][data-detail-viewer-zoom]',
    );
    await slider.waitFor({ state: "attached", timeout: 5000 });

    // ── 4. Change zoom via slider event ───────────────────────────────────
    // Set the slider value and dispatch change event for WASM handler
    await slider.evaluate((el) => {
      el.value = "150";
      el.dispatchEvent(new Event("change", { bubbles: true }));
    });

    // Wait for UI update (WASM applies zoom to #sb-main style)
    await page.waitForTimeout(500);

    const zoomAfter = await page.getAttribute("#sb-main", "data-zoom");

    if (Deno.env.get("DEBUG"))
      console.log(`Zoom after change: ${zoomAfter}`);
    if (zoomAfter !== "150") {
      throw new Error(
        `Zoom was not updated by WASM handler: expected 150, got ${zoomAfter}`,
      );
    }

    // ── 5. Reload and verify zoom persists (SSR) ──────────────────────────
    // Use a separate JS-disabled context so WASM doesn't interfere
    const contextNoJs = await browser.newContext({
      javaScriptEnabled: false,
    });
    const pageNoJs = await contextNoJs.newPage();
    await contextNoJs.addCookies(cookies);
    await pageNoJs.goto(`${BASE}/gig/${sbId}`, GOTO);
    const zoomSsr = await pageNoJs.getAttribute("#sb-main", "data-zoom");

    if (Deno.env.get("DEBUG"))
      console.log(`Zoom SSR: ${zoomSsr}`);
    if (zoomSsr !== "150") {
      throw new Error(
        `Zoom did not persist via SSR: expected 150, got ${zoomSsr}`,
      );
    }

    // ── 6. Cross-module: gig → song page ─────────────────────────────
    // Toggle latin/video on the gig page; the WASM persists all
    // settings (l/m/z) via GET /api/song/prefs.
    for (const name of ["l", "m"]) {
      await page.locator(`input[type="checkbox"][name="${name}"]`)
        .evaluate((el) => {
          (el as unknown as { checked: boolean }).checked = true;
          el.dispatchEvent(new Event("change", { bubbles: true }));
        });
    }
    await page.waitForTimeout(600);

    // ── 6.1 Remove URL params and reload (JS enabled): saved prefs must
    // come back. The WASM rewrote the URL to ?l=&m=&z= after the toggles;
    // stripping them and reloading must restore the saved settings via SSR.
    const paramUrl = page.url();
    if (!paramUrl.includes("?"))
      throw new Error(`Expected URL params after toggles, got ${paramUrl}`);
    await page.goto(`${BASE}/gig/${sbId}`, GOTO);
    await page.waitForSelector('input[type="range"][data-detail-viewer-zoom]');
    await page.waitForTimeout(400); // let WASM hydrate from embedded state
    const noParamZoom = await page.getAttribute("#sb-main", "data-zoom");
    const noParamLatin = await page.locator(
      'input[type="checkbox"][name="l"]').isChecked();
    const noParamMedia = await page.locator(
      'input[type="checkbox"][name="m"]').isChecked();
    if (noParamZoom !== "150" || !noParamLatin || !noParamMedia) {
      throw new Error(
        `Removing URL params lost saved prefs: zoom=${noParamZoom} ` +
        `latin=${noParamLatin} media=${noParamMedia}`,
      );
    }

    const pageSong = await contextNoJs.newPage();
    await pageSong.goto(`${BASE}/song/${SONG_ID}`, GOTO);
    const songZoom = await pageSong.getAttribute("#main", "data-zoom");
    const songLatin = await pageSong.getAttribute("#main", "data-use-latin");
    const songMedia = await pageSong.getAttribute("#main", "data-show-media");
    if (songZoom !== "150" || songLatin !== "1" || songMedia !== "1") {
      throw new Error(
        `Song page not synced from gig: zoom=${songZoom} ` +
        `latin=${songLatin} media=${songMedia}`,
      );
    }

    // ── 7. Cross-module: song page → gig ─────────────────────────────
    // The song transpose endpoint persists z/l/m prefs for the user.
    const syncResp = await fetch(
      `${BASE}/api/song/${SONG_ID}/transpose?z=110&l=0&m=0`,
      { headers: { Cookie: cookieHeader }, redirect: "manual" },
    );
    if (syncResp.status >= 400)
      throw new Error(`Song transpose persist failed: ${syncResp.status}`);
    await syncResp.body?.cancel();

    const pageSb2 = await contextNoJs.newPage();
    await pageSb2.goto(`${BASE}/gig/${sbId}`, GOTO);
    const sbZoom2 = await pageSb2.getAttribute("#sb-main", "data-zoom");
    if (sbZoom2 !== "110") {
      throw new Error(
        `Gig not synced from song page: expected 110, got ${sbZoom2}`,
      );
    }

    // Clean up: reset zoom to 100
    await page.goto(`${BASE}/gig/${sbId}`, GOTO);
    await page.waitForSelector(
      'input[type="range"][data-detail-viewer-zoom]',
    );
    await page.locator('input[type="range"][data-detail-viewer-zoom]')
      .evaluate((el) => {
        el.value = "100";
        el.dispatchEvent(new Event("change", { bubbles: true }));
      });

    await contextNoJs.close();

  } catch (e) {
    console.log("BROWSER LOGS:");
    for (const l of browserLogs) console.log(l);
    throw e;
  } finally {
    if (Deno.env.get("DEBUG")) {
      console.log("BROWSER LOGS:");
      for (const l of browserLogs) console.log(l);
    }
    await browser.close();
  }
});
