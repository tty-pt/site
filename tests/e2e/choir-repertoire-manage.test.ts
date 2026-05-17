/**
 * E2E test: choir repertoire management
 *
 * Tests:
 *   1. Add song to choir repertoire
 *   2. Set song key in choir repertoire
 *   3. Remove song from choir repertoire
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";
const SONG_TITLE = "A alegria está no coração";

Deno.test({
  name: "choir repertoire: add, set key, and remove song",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let choirId: string | null = null;

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    // 1. Create a choir
    const choirTitle = `Repertoire Test Choir ${Date.now()}`;
    await page.goto(`${BASE}/choir/add`);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', choirTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/choir\/[^/]+$/);
    choirId = page.url().split("/choir/")[1].replace(/\/$/, "");

    // 2. Add song to choir
    const { token: csrfAdd, cookieHeader: chAdd } = await getCsrfToken(cookieHeader, BASE);
    const addBody = new URLSearchParams({
      song_id: SONG_ID,
      format: "any",
      csrf_token: csrfAdd
    });
    const addResp = await fetch(`${BASE}/api/choir/${choirId}/songs`, {
      method: "POST",
      body: addBody.toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chAdd },
    });
    if (addResp.status >= 400) throw new Error(`Add song failed: ${addResp.status}`);
    await addResp.body?.cancel();

    await page.goto(`${BASE}/choir/${choirId}`);
    await waitForText(page, "body", SONG_TITLE);

    // 3. Set song key (e.g. key = 5)
    // The endpoint is POST /api/choir/:id/song/:song_id/key
    const { token: csrfKey, cookieHeader: chKey } = await getCsrfToken(cookieHeader, BASE);
    const keyBody = new URLSearchParams({
      key: "5",
      csrf_token: csrfKey
    });
    const keyResp = await fetch(`${BASE}/api/choir/${choirId}/song/${SONG_ID}/key`, {
      method: "POST",
      body: keyBody.toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chKey },
    });
    if (keyResp.status >= 400) throw new Error(`Set key failed: ${keyResp.status}`);
    await keyResp.body?.cancel();

    // Verify key in choir detail (it might not be visible in UI, but we check if it still renders)
    await page.goto(`${BASE}/choir/${choirId}`);
    await waitForText(page, "body", SONG_TITLE);

    // 4. Remove song from choir
    // The endpoint is POST /api/choir/:id/song/:song_id/remove
    const { token: csrfRem, cookieHeader: chRem } = await getCsrfToken(cookieHeader, BASE);
    const remBody = new URLSearchParams({
      csrf_token: csrfRem
    });
    const remResp = await fetch(`${BASE}/api/choir/${choirId}/song/${SONG_ID}/remove`, {
      method: "POST",
      body: remBody.toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chRem },
    });
    if (remResp.status >= 400) throw new Error(`Remove song failed: ${remResp.status}`);
    await remResp.body?.cancel();

    // Verify song is gone
    await page.goto(`${BASE}/choir/${choirId}`);
    const content = await page.textContent("body") ?? "";
    if (content.includes(SONG_TITLE)) {
      throw new Error("Song still present in choir repertoire after removal");
    }

  } finally {
    await browser.close();
  }
});
