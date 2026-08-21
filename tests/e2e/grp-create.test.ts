/**
 * E2E test: grp create → detail page → edit form → edit title → add song
 *           → update preferred key → view grp song → remove song.
 *
 * Tests:
 *   1. Authenticated user can create a grp via /grp/add
 *   2. Redirects to grp detail page after creation
 *   3. Detail page shows the owner username and the Edit/Delete menu
 *   4. Edit form is pre-populated with the grp title
 *   5. Cancel returns to the detail page
 *   6. Submit edit form with a new title → verify updated title on detail page
 *   7. Add a known song to grp repertoire → verify it appears on detail page
 *   8. Update preferred key for the song via /api/grp/:id/song/:song_id/key
 *   9. View the grp song page (GET /grp/:id/song/:song_id)
 *  10. Delete song from grp → verify it no longer appears on detail page
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken, waitForText } from "./helpers/auth.ts";
import { setupRequestLogging, withDebugCapture } from "./helpers/debug.ts";

const BASE = "http://localhost:8080";
// A song that always exists in the test data
const KNOWN_SONG_ID = "a_alegria_esta_no_coracao";
const KNOWN_SONG_TITLE = "A alegria está no coração";

Deno.test({ name: "grp: register → login → create grp → view detail → edit form → edit title → add song", sanitizeResources: false, sanitizeOps: false }, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  setupRequestLogging(page);

  try {
    await withDebugCapture(page, "grp-create", async () => {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      // Block static asset requests to avoid keeping connections open
      // and stalling subsequent API calls. We only need the SSR HTML.
      await page.route("**/styles.css", (route) => route.abort());
      await page.route("**/favicon.ico", (route) => route.abort());

      const user = await createAndLoginUser(page, BASE);
      const grpTitle = `Test Grp ${Date.now()}`;

      const GOTO = { waitUntil: "domcontentloaded" as const };

      // ── 1. Create grp via /grp/add ──────────────────────────────────────
      await page.goto(`${BASE}/grp/add`, GOTO);
      await page.waitForSelector('input[name="title"]', { timeout: 5000 });
      await page.fill('input[name="title"]', grpTitle);
      await page.click('button[type="submit"]');

      // Should redirect to /grp/<id> after creation
      await page.waitForURL(/\/grp\/[^/]+$/, { timeout: 5000 });

      // ── 2. Verify detail page shows title and owner ─────────────────────────
      await page.waitForSelector("h1", { timeout: 5000 });
      await waitForText(page, "body", grpTitle);
      await waitForText(page, "body", user.username);

      // Extract grp ID from URL
      const grpUrl = page.url();
      const grpId = grpUrl.split("/grp/")[1];

      // ── 3. Verify Edit and Delete buttons appear in menu ────────────────────
      await page.waitForSelector('a[href*="/edit"]', { timeout: 5000 });
      const editLink = await page.getAttribute('a[href*="/edit"]', "href");
      if (!editLink?.includes(grpId)) {
        throw new Error(`Edit link "${editLink}" does not belong to grp ${grpId}`);
      }

      // ── 4. Navigate to edit form and verify pre-population ─────────────────
      await page.goto(`${BASE}/grp/${grpId}/edit`, GOTO);
      await page.waitForSelector('input[name="title"]', { timeout: 5000 });

      const formTitle = await page.inputValue('input[name="title"]');
      if (formTitle !== grpTitle) {
        throw new Error(
          `Edit form title mismatch. Expected "${grpTitle}", got "${formTitle}"`,
        );
      }

      // ── 5. Cancel returns to detail page ────────────────────────────────────
      await page.click('a[href*="/grp/' + grpId + '"]');
      await page.waitForURL(new RegExp(`/grp/${grpId}(/|$)`), { timeout: 8000 });
      await waitForText(page, "body", grpTitle);

      // Extract session cookie for out-of-browser API calls (avoids pipelining hangs)
      const cookies = await page.context().cookies();
      const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

      // ── 6. Edit grp title via form submit ─────────────────────────────────
      const updatedTitle = `${grpTitle} (updated)`;
      const { token: csrf6, cookieHeader: ch6 } = await getCsrfToken(cookieHeader, BASE);
      const fd6 = new FormData();
      fd6.append("title", updatedTitle);
      fd6.append("csrf_token", csrf6);
      const editResp = await fetch(`${BASE}/grp/${grpId}/edit`, {
        method: "POST",
        body: fd6,
        headers: { Cookie: ch6 },
      });
      if (editResp.status !== 200 && editResp.status !== 303) {
        throw new Error(`Grp edit POST returned unexpected status ${editResp.status}`);
      }
      await editResp.body?.cancel();

      // Reload detail page and verify updated title
      await page.goto(`${BASE}/grp/${grpId}`, GOTO);
      await page.waitForSelector("h1", { timeout: 5000 });
      await waitForText(page, "body", updatedTitle);

      // ── 7. Add known song to grp repertoire via the list-grade picker ──────
      // Omni mode hides Apply; Enter submits the search. The whole-row
      // submit button carries the song id as its value.
      await page.goto(`${BASE}/grp/${grpId}`, GOTO);
      await page.waitForSelector('form.list-form input[name="q"]', { timeout: 5000 });
      await page.fill('form.list-form input[name="q"]', KNOWN_SONG_TITLE);
      await page.press('form.list-form input[name="q"]', "Enter");
      const pickBtn = `button.hyle-row-action[value="${KNOWN_SONG_ID}"]`;
      await page.waitForSelector(pickBtn, { timeout: 8000 });
      await Promise.all([
        page.waitForURL(new RegExp(`/grp/${grpId}(/|$)`), { timeout: 8000 }),
        page.click(pickBtn),
      ]);
      await page.waitForSelector('button:has-text("Remove")', { timeout: 5000 });

      // ── 8. Update preferred key for the song via UI ─────────────────────────
      await page.waitForSelector('select[name="key"]');
      await page.selectOption('select[name="key"]', "5");
      await Promise.all([
        page.waitForURL(new RegExp(`/grp/${grpId}(/|$)`), { timeout: 8000 }),
        page.click('button:has-text("Set")'),
      ]);
      await page.waitForSelector('button:has-text("Remove")', { timeout: 5000 });

      // ── 9. View the grp song page via browser (follows redirect to /song/:id?t=X) ──
      await page.goto(`${BASE}/grp/${grpId}/song/${KNOWN_SONG_ID}`, GOTO);
      await page.waitForSelector("h1", { state: "attached", timeout: 5000 });
      await page.waitForSelector("h1", { timeout: 10000 });
      await waitForText(page, "body", KNOWN_SONG_TITLE, 5000);

      // ── 10. Delete song from grp repertoire via UI ────────────────────────
      await page.goto(`${BASE}/grp/${grpId}`, GOTO);
      await page.waitForSelector("h1", { timeout: 5000 });
      await Promise.all([
        page.waitForURL(new RegExp(`/grp/${grpId}(/|$)`), { timeout: 8000 }),
        page.click('button:has-text("Remove")'),
      ]);
      await waitForText(page, "body", "No songs in repertoire yet");
    });
  } finally {
    await browser.close();
  }
});
