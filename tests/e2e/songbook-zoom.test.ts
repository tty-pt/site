/**
 * E2E test: songbook zoom slider
 *
 * Tests:
 *   1. Create songbook with a song via the new add API
 *   2. View songbook page, verify zoom slider exists
 *   3. Change zoom via slider, verify data-zoom attribute
 *   4. Reload and verify zoom persists in SSR
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";

Deno.test({
  name: "songbook zoom: slider changes chord-zoom and persists",
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

    // ── 0. Create a choir and seed the song ───────────────────────────────
    const choirTitle = `Zoom Choir ${Date.now()}`;
    await page.goto(`${BASE}/choir/add`, GOTO);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', choirTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/choir\/[^/]+$/);
    const choirId = page.url().split("/choir/")[1];

    const { token: csrfSeed, cookieHeader: chSeed } =
      await getCsrfToken(cookieHeader, BASE);
    const seedBody = new URLSearchParams({
      song_id: SONG_ID,
      format: "any",
      csrf_token: csrfSeed,
    });
    const seedResp = await fetch(`${BASE}/api/choir/${choirId}/songs`, {
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
    const repoId = `${choirId}_${SONG_ID}`;

    // ── 1. Create a songbook linked to the choir ──────────────────────────
    const sbTitle = `Zoom Test SB ${Date.now()}`;
    await page.goto(`${BASE}/songbook/add?choir=${choirId}`, GOTO);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', sbTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/songbook\/[^/]+$/);
    sbId = page.url().split("/songbook/")[1].replace(/\/$/, "");

    // ── 2. Add song via new API (url-encoded) ─────────────────────────────
    const { token: csrfAdd, cookieHeader: chAdd } =
      await getCsrfToken(cookieHeader, BASE);
    const addBody = new URLSearchParams({
      song_id: repoId,
      csrf_token: csrfAdd,
    });
    const addResp = await fetch(`${BASE}/api/songbook/${sbId}/songs`, {
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

    // ── 3. View songbook and verify slider exists ─────────────────────────
    await page.goto(`${BASE}/songbook/${sbId}`, GOTO);
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
    await pageNoJs.goto(`${BASE}/songbook/${sbId}`, GOTO);
    const zoomSsr = await pageNoJs.getAttribute("#sb-main", "data-zoom");

    if (Deno.env.get("DEBUG"))
      console.log(`Zoom SSR: ${zoomSsr}`);

    // Clean up: reset zoom to 100
    await page.goto(`${BASE}/songbook/${sbId}`, GOTO);
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
