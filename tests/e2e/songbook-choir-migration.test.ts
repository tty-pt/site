/**
 * E2E test: songbook -> choir reverse migration flow
 *
 * Verifies that editing a songbook linked to a choir triggers
 * migrate_songbook_to_choir(), adding new songs to the choir's repertoire
 * while never removing existing ones.
 *
 * Tests:
 *   1. Create choir, add songs to repertoire
 *   2. Set choir format, create songbook linked to choir (auto-populated)
 *   3. Edit songbook adding a new song not in repertoire
 *   4. Verify migration: new song appears in choir repertoire
 *   5. Edit songbook again (remove one song)
 *   6. Verify removed song still in repertoire (additive migration)
 *   7. Verify songbook view renders current songs
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken, waitForText } from "./helpers/auth.ts";

const REPO_ROOT = new URL("../../", import.meta.url).pathname.replace(/\/$/, "");
const BASE = "http://localhost:8080";

const SONG1_ID = "a_alegria_esta_no_coracao";
const SONG1_TITLE = "A alegria está no coração";
const SONG2_ID = "abencoai_a_nossa_oferta";
const SONG2_TITLE = "Abençoai a nossa oferta";
const SONG3_ID = "fomos_resgatados";
const SONG3_TITLE = "Fomos resgatados";

Deno.test({
  name: "songbook->choir: edit songbook migrates new songs to choir repertoire, never removes",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  let choirId: string | null = null;
  let sbId: string | null = null;

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await page.route("**/_frsh/js/**", (route) => route.abort());
    await page.route("**/styles.css", (route) => route.abort());
    await page.route("**/favicon.ico", (route) => route.abort());

    const GOTO = { waitUntil: "domcontentloaded" as const };

    await createAndLoginUser(page, BASE);

    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    // -- 1. Create choir ----------------------------------------------------------
    const choirTitle = `Migration Choir ${Date.now()}`;
    await page.goto(`${BASE}/choir/add`, GOTO);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', choirTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/choir\/[^/]+$/, { timeout: 5000 });
    choirId = page.url().split("/choir/")[1];

    // -- 2. Add 2 songs to choir repertoire ---------------------------------------
    for (const songId of [SONG1_ID, SONG2_ID]) {
      const { token: csrf, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);
      const body = new URLSearchParams({ song_id: songId, format: "any", csrf_token: csrf });
      const r = await fetch(`${BASE}/api/choir/${choirId}/songs`, {
        method: "POST",
        body: body.toString(),
        headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: ch },
      });
      if (r.status >= 400) throw new Error(`Add song ${songId} failed: ${r.status}`);
      await r.body?.cancel();
    }

    // -- 3. Set choir format to "any" ----------------------------------------------
    {
      const { token: csrf, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);
      const editFd = new FormData();
      editFd.append("title", choirTitle);
      editFd.append("format", "any");
      editFd.append("csrf_token", csrf);
      const r = await fetch(`${BASE}/choir/${choirId}/edit`, {
        method: "POST",
        body: editFd,
        headers: { Cookie: ch },
        redirect: "manual",
      });
      if (r.status >= 400) throw new Error(`Choir edit failed: ${r.status}`);
      await r.body?.cancel();
    }

    // -- 4. Create songbook linked to choir (auto-populates from repertoire) -------
    const sbTitle = `Migration SB ${Date.now()}`;
    await page.goto(`${BASE}/songbook/add?choir=${choirId}`, GOTO);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', sbTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/songbook\/[^/]+$/, { timeout: 5000 });
    sbId = page.url().split("/songbook/")[1];

    // -- 5. Verify auto-population -------------------------------------------------
    await page.goto(`${BASE}/songbook/${sbId}`, GOTO);
    await page.waitForSelector("body");
    const songItems = await page.$$('[data-songbook-item]');
    if (songItems.length === 0) {
      throw new Error("No pre-populated song items rendered on detail page");
    }

    // -- 6. Edit songbook: replace pre-populated songs with SONG1+SONG2+SONG3 ------
    {
      const { token: csrf, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);
      const fd = new FormData();
      fd.append("amount", "3");
      fd.append("song_0", `${SONG1_TITLE} [${SONG1_ID}]`);
      fd.append("key_0", "0");
      fd.append("fmt_0", "any");
      fd.append("song_1", `${SONG2_TITLE} [${SONG2_ID}]`);
      fd.append("key_1", "0");
      fd.append("fmt_1", "any");
      fd.append("song_2", `${SONG3_TITLE} [${SONG3_ID}]`);
      fd.append("key_2", "0");
      fd.append("fmt_2", "any");
      fd.append("csrf_token", csrf);

      const editResp = await fetch(`${BASE}/songbook/${sbId}/edit`, {
        method: "POST",
        body: fd,
        headers: { Cookie: ch },
        redirect: "manual",
      });
      if (editResp.status >= 400) {
        const txt = await editResp.text();
        throw new Error(`Songbook edit POST failed ${editResp.status}: ${txt.slice(0, 200)}`);
      }
      await editResp.body?.cancel();
    }

    // -- 7. Verify SONG3 now in choir repertoire (migration worked) ----------------
    await page.goto(`${BASE}/choir/${choirId}`, GOTO);
    await waitForText(page, "body", SONG3_TITLE);
    await waitForText(page, "body", SONG1_TITLE);
    await waitForText(page, "body", SONG2_TITLE);

    // -- 8. Edit songbook again: remove SONG1 (keep SONG2 + SONG3) ----------------
    {
      const { token: csrf, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);
      const fd = new FormData();
      fd.append("amount", "2");
      fd.append("song_0", `${SONG2_TITLE} [${SONG2_ID}]`);
      fd.append("key_0", "0");
      fd.append("fmt_0", "any");
      fd.append("song_1", `${SONG3_TITLE} [${SONG3_ID}]`);
      fd.append("key_1", "0");
      fd.append("fmt_1", "any");
      fd.append("csrf_token", csrf);

      const editResp = await fetch(`${BASE}/songbook/${sbId}/edit`, {
        method: "POST",
        body: fd,
        headers: { Cookie: ch },
        redirect: "manual",
      });
      if (editResp.status >= 400) {
        const txt = await editResp.text();
        throw new Error(`Second songbook edit POST failed ${editResp.status}: ${txt.slice(0, 200)}`);
      }
      await editResp.body?.cancel();
    }

    // -- 9. Verify SONG1 still in repertoire (migration is additive) --------------
    await page.goto(`${BASE}/choir/${choirId}`, GOTO);
    await waitForText(page, "body", SONG1_TITLE);
    await waitForText(page, "body", SONG2_TITLE);
    await waitForText(page, "body", SONG3_TITLE);

    // -- 10. Verify songbook view renders current songs ---------------------------
    await page.goto(`${BASE}/songbook/${sbId}`, GOTO);
    await waitForText(page, "body", SONG2_TITLE);
    await waitForText(page, "body", SONG3_TITLE);

  } finally {
    if (sbId) {
      try {
        const sbPath = `${REPO_ROOT}/items/songbook/items/${sbId}`;
        for await (const entry of Deno.readDir(sbPath)) {
          await Deno.remove(`${sbPath}/${entry.name}`);
        }
        await Deno.remove(sbPath);
      } catch { /* ignore */ }
    }
    await browser.close();
  }
});
