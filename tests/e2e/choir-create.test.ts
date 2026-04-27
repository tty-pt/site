/**
 * E2E test: choir create → detail page → edit form → edit title → add song
 *           → update preferred key → view choir song → remove song.
 *
 * Tests:
 *   1. Authenticated user can create a choir via /choir/add
 *   2. Redirects to choir detail page after creation
 *   3. Detail page shows the owner username and the Edit/Delete menu
 *   4. Edit form is pre-populated with the choir title
 *   5. Cancel returns to the detail page
 *   6. Submit edit form with a new title → verify updated title on detail page
 *   7. Add a known song to choir repertoire → verify it appears on detail page
 *   8. Update preferred key for the song via /api/choir/:id/song/:song_id/key
 *   9. View the choir song page (GET /choir/:id/song/:song_id)
 *  10. Delete song from choir → verify it no longer appears on detail page
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
// A song that always exists in the test data
const KNOWN_SONG_ID = "a_alegria_esta_no_coracao";
const KNOWN_SONG_TITLE = "A alegria está no coração";

Deno.test({ name: "choir: register → login → create choir → view detail → edit form → edit title → add song", sanitizeResources: false, sanitizeOps: false }, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    // Block static asset requests that would pile up on the keep-alive connection
    // and stall subsequent API calls. We only need the SSR HTML for our checks.
    await page.route("**/_frsh/js/**", (route) => route.abort());
    await page.route("**/styles.css", (route) => route.abort());
    await page.route("**/app.js", (route) => route.abort());
    await page.route("**/favicon.ico", (route) => route.abort());

    const user = await createAndLoginUser(page, BASE);
    const choirTitle = `Test Choir ${Date.now()}`;

    const GOTO = { waitUntil: "domcontentloaded" as const };

    // ── 1. Create choir via /choir/add ──────────────────────────────────────
    await page.goto(`${BASE}/choir/add`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', choirTitle);
    await page.click('button[type="submit"]');

    // Should redirect to /choir/<id> after creation
    await page.waitForURL(/\/choir\//, { timeout: 5000 });

    // ── 2. Verify detail page shows title and owner ─────────────────────────
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "body", choirTitle);
    await waitForText(page, "body", user.username);

    // Extract choir ID from URL
    const choirUrl = page.url();
    const choirId = choirUrl.split("/choir/")[1];

    // ── 3. Verify Edit and Delete buttons appear in menu ────────────────────
    await page.waitForSelector('a[href*="/edit"]', { timeout: 5000 });
    const editLink = await page.getAttribute('a[href*="/edit"]', "href");
    if (!editLink?.includes(choirId)) {
      throw new Error(`Edit link "${editLink}" does not belong to choir ${choirId}`);
    }

    // ── 4. Navigate to edit form and verify pre-population ─────────────────
    await page.goto(`${BASE}/choir/${choirId}/edit`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });

    const formTitle = await page.inputValue('input[name="title"]');
    if (formTitle !== choirTitle) {
      throw new Error(
        `Edit form title mismatch. Expected "${choirTitle}", got "${formTitle}"`,
      );
    }

    // ── 5. Cancel returns to detail page ────────────────────────────────────
    await page.click('a[href="/choir/' + choirId + '"]');
    await page.waitForURL(`${BASE}/choir/${choirId}`, { timeout: 5000 });
    await waitForText(page, "body", choirTitle);

    // Extract session cookie for out-of-browser API calls (avoids pipelining hangs)
    const cookies = await page.context().cookies();
    const sessionCookie = cookies.find((c) => c.name === "session" || c.name === "token" || c.name === "sid");
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    // Helper: perform API calls using Deno's native fetch (outside browser).
    // This avoids HTTP pipelining issues on NDC's single-fd-per-connection model.
    const apiFetch = (url: string, init?: RequestInit) =>
      fetch(url, {
        ...init,
        headers: {
          ...(init?.headers as Record<string, string> ?? {}),
          "Cookie": cookieHeader,
        },
      });

    // ── 6. Edit choir title via form submit ─────────────────────────────────
    const updatedTitle = `${choirTitle} (updated)`;
    const fd6 = new FormData();
    fd6.append("title", updatedTitle);
    const editResp = await apiFetch(`${BASE}/api/choir/${choirId}/edit`, { method: "POST", body: fd6 });
    if (editResp.status !== 200 && editResp.status !== 303) {
      throw new Error(`Choir edit POST returned unexpected status ${editResp.status}`);
    }
    await editResp.body?.cancel();

    // Reload detail page and verify updated title
    await page.goto(`${BASE}/choir/${choirId}`, GOTO);
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "body", updatedTitle);

    // ── 7. Add known song to choir repertoire ───────────────────────────────
    const body7 = new URLSearchParams({ song_id: KNOWN_SONG_ID, format: "any" });
    const addSongResp = await apiFetch(`${BASE}/api/choir/${choirId}/songs`, {
      method: "POST",
      body: body7.toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
    });
    if (addSongResp.status >= 400) {
      throw new Error(`Add song to choir returned unexpected status ${addSongResp.status}`);
    }
    await addSongResp.body?.cancel();

    // Reload and verify song appears in the repertoire list
    await page.goto(`${BASE}/choir/${choirId}`, GOTO);
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "body", KNOWN_SONG_TITLE, 6000);

    // ── 8. Update preferred key for the song ────────────────────────────────
    const body8 = new URLSearchParams({ key: "5" });
    const keyResp = await apiFetch(`${BASE}/api/choir/${choirId}/song/${KNOWN_SONG_ID}/key`, {
      method: "POST",
      body: body8.toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      redirect: "manual",
    });
    if (keyResp.status >= 400) {
      throw new Error(`Key update returned unexpected status ${keyResp.status}`);
    }
    await keyResp.body?.cancel();

    // ── 9. View the choir song page via browser (follows redirect to /song/:id?t=X) ──
    await page.goto(`${BASE}/choir/${choirId}/song/${KNOWN_SONG_ID}`, GOTO);
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "body", KNOWN_SONG_TITLE, 5000);

    // ── 10. Delete song from choir ───────────────────────────────────────────
    // Note: NDC doesn't support DELETE, use POST /remove endpoint instead
    const deleteResp = await apiFetch(`${BASE}/api/choir/${choirId}/song/${KNOWN_SONG_ID}/remove`, {
      method: "POST",
      redirect: "manual",
    });
    if (deleteResp.status >= 400) {
      throw new Error(`Song delete returned unexpected status ${deleteResp.status}`);
    }
    await deleteResp.body?.cancel();

    // Reload and verify song no longer appears in repertoire list
    await page.goto(`${BASE}/choir/${choirId}`, GOTO);
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "body", "No songs in repertoire yet");
  } finally {
    await browser.close();
  }
});
