/**
 * E2E test: gig -> grp reverse migration flow
 *
 * Verifies that editing a gig linked to a grp triggers
 * migrate_gig_to_grp(), adding new songs to the grp's repertoire
 * while never removing existing ones.
 *
 * Tests:
 *   1. Create grp, add songs to repertoire
 *   2. Set grp format, create gig linked to grp (auto-populated)
 *   3. Edit gig adding a new song not in repertoire
 *   4. Verify migration: new song appears in grp repertoire
 *   5. Edit gig again (remove one song)
 *   6. Verify removed song still in repertoire (additive migration)
 *   7. Verify gig view renders current songs
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
  name: "gig->grp: edit gig migrates new songs to grp repertoire, never removes",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  let grpId: string | null = null;
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

    // -- 1. Create grp ----------------------------------------------------------
    const grpTitle = `Migration Grp ${Date.now()}`;
    await page.goto(`${BASE}/grp/add`, GOTO);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', grpTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/, { timeout: 5000 });
    grpId = page.url().split("/grp/")[1];

    // -- 2. Add 2 songs to grp repertoire ---------------------------------------
    for (const songId of [SONG1_ID, SONG2_ID]) {
      const { token: csrf, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);
      const body = new URLSearchParams({ song_id: songId, format: "any", csrf_token: csrf });
      const r = await fetch(`${BASE}/api/grp/${grpId}/songs`, {
        method: "POST",
        body: body.toString(),
        headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: ch },
      });
      if (r.status >= 400) throw new Error(`Add song ${songId} failed: ${r.status}`);
      await r.body?.cancel();
    }

    // -- 3. Set grp format to "any" ----------------------------------------------
    {
      const { token: csrf, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);
      const editFd = new FormData();
      editFd.append("title", grpTitle);
      editFd.append("format", "any");
      editFd.append("csrf_token", csrf);
      const r = await fetch(`${BASE}/grp/${grpId}/edit`, {
        method: "POST",
        body: editFd,
        headers: { Cookie: ch },
        redirect: "manual",
      });
      if (r.status >= 400) throw new Error(`Grp edit failed: ${r.status}`);
      await r.body?.cancel();
    }

    // -- 4. Create gig linked to grp (auto-populates from repertoire) -------
    const sbTitle = `Migration SB ${Date.now()}`;
    await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
    await page.waitForSelector('input[name="title"]');
    await page.fill('input[name="title"]', sbTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 5000 });
    sbId = page.url().split("/gig/")[1];

    // -- 5. Verify auto-population -------------------------------------------------
    await page.goto(`${BASE}/gig/${sbId}`, GOTO);
    await page.waitForSelector("body");
    const songItems = await page.$$('[data-gig-item]');
    if (songItems.length === 0) {
      throw new Error("No pre-populated song items rendered on detail page");
    }

    // -- 6. Edit gig: replace pre-populated songs with SONG1+SONG2+SONG3 ------
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

      const editResp = await fetch(`${BASE}/gig/${sbId}/edit`, {
        method: "POST",
        body: fd,
        headers: { Cookie: ch },
        redirect: "manual",
      });
      if (editResp.status >= 400) {
        const txt = await editResp.text();
        throw new Error(`Gig edit POST failed ${editResp.status}: ${txt.slice(0, 200)}`);
      }
      await editResp.body?.cancel();
    }

    // -- 7. Verify SONG3 now in grp repertoire (migration worked) ----------------
    await page.goto(`${BASE}/grp/${grpId}`, GOTO);
    await waitForText(page, "body", SONG3_TITLE);
    await waitForText(page, "body", SONG1_TITLE);
    await waitForText(page, "body", SONG2_TITLE);

    // -- 8. Edit gig again: remove SONG1 (keep SONG2 + SONG3) ----------------
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

      const editResp = await fetch(`${BASE}/gig/${sbId}/edit`, {
        method: "POST",
        body: fd,
        headers: { Cookie: ch },
        redirect: "manual",
      });
      if (editResp.status >= 400) {
        const txt = await editResp.text();
        throw new Error(`Second gig edit POST failed ${editResp.status}: ${txt.slice(0, 200)}`);
      }
      await editResp.body?.cancel();
    }

    // -- 9. Verify SONG1 still in repertoire (migration is additive) --------------
    await page.goto(`${BASE}/grp/${grpId}`, GOTO);
    await waitForText(page, "body", SONG1_TITLE);
    await waitForText(page, "body", SONG2_TITLE);
    await waitForText(page, "body", SONG3_TITLE);

    // -- 10. Verify gig view renders current songs ---------------------------
    await page.goto(`${BASE}/gig/${sbId}`, GOTO);
    await waitForText(page, "body", SONG2_TITLE);
    await waitForText(page, "body", SONG3_TITLE);

  } finally {
    if (sbId) {
      try {
        const sbPath = `${REPO_ROOT}/var/gig/${sbId}`;
        for await (const entry of Deno.readDir(sbPath)) {
          await Deno.remove(`${sbPath}/${entry.name}`);
        }
        await Deno.remove(sbPath);
      } catch { /* ignore */ }
    }
    await browser.close();
  }
});
