/**
 * E2E test: gig create → edit page → save → verify redirect.
 *
 * Tests:
 *   1. Authenticated user can create a gig via /gig/add
 *   2. GET /gig/<id>/edit renders the edit form with the gig title,
 *      the omnisearch picker, and the read-only song row contract
 *      (hidden song_N + 🔄 Change link instead of per-row selects)
 *   3. Adding a song via the API shows up as a row; POST /gig/<id>/edit
 *      with that row (amount=N + song_i/key_i/fmt_i) redirects to view
 *   4. After save, the view page still shows the gig title and the song
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken, waitForText } from "./helpers/auth.ts";

const REPO_ROOT = new URL("../../", import.meta.url).pathname.replace(/\/$/, "");
const BASE = "http://localhost:8080";
const SEED_SONG_ID = "a_alegria_esta_no_coracao";
const SEED_SONG_TITLE = "A alegria está no coração";

Deno.test("gig: register → login → create gig → load edit page → save → view", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  let sbId: string | null = null;

  try {
    await createAndLoginUser(page, BASE);
    const sbTitle = `SB Edit Test ${Date.now()}`;

    // ── 1. Create gig via /gig/add ────────────────────────────────
    await page.goto(`${BASE}/gig/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', sbTitle);
    await page.click('button[type="submit"]');

    await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 5000 });

    const sbUrl = page.url();
    sbId = sbUrl.split("/gig/")[1];

    // ── 2. Load the edit page ───────────────────────────────────────────────
    await page.goto(`${BASE}/gig/${sbId}/edit`);
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "h1", sbTitle);

    // Verify the Save Changes button (the omnisearch picker also has
    // a submit button, so target the main form's button explicitly)
    await page.waitForSelector("#edit-form-submit", { timeout: 5000 });

    // Empty gig: amount hidden field is 0, no per-song rows yet
    const amountEl = page.locator('input[name="amount"]');
    await amountEl.waitFor({ state: "attached", timeout: 5000 });
    const amountVal = await amountEl.getAttribute("value");
    if (amountVal !== "0") {
      throw new Error(`Expected amount=0 on empty gig edit, got ${amountVal}`);
    }

    // ── 3. Seed a song via API, verify row contract, then save ────────────
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");
    const { token: csrfSeed, cookieHeader: chSeed } =
      await getCsrfToken(cookieHeader, BASE);
    const seedBody = new URLSearchParams({
      song_id: SEED_SONG_ID,
      csrf_token: csrfSeed,
    });
    const seedResp = await fetch(`${BASE}/api/gig/${sbId}/songs`, {
      method: "POST",
      body: seedBody.toString(),
      headers: {
        "Content-Type": "application/x-www-form-urlencoded",
        Cookie: chSeed,
      },
      redirect: "manual",
    });
    if (seedResp.status >= 400) {
      throw new Error(`Seeding song failed: HTTP ${seedResp.status}`);
    }
    await seedResp.body?.cancel();

    // Reload the edit page: the seeded song must render as a row with
    // the new contract (hidden song_N + Change link, no <select>)
    await page.goto(`${BASE}/gig/${sbId}/edit`);
    const amountEl2 = page.locator('input[name="amount"]');
    await amountEl2.waitFor({ state: "attached", timeout: 5000 });

    const amountAfter = await amountEl2.getAttribute("value");
    if (amountAfter !== "1") {
      throw new Error(`Expected amount=1 after seeding a song, got ${amountAfter}`);
    }

    const songRow = await page.inputValue('input[name="song_0"]');
    if (songRow !== SEED_SONG_ID) {
      throw new Error(
        `Expected hidden song_0=${SEED_SONG_ID}, got "${songRow}"`,
      );
    }

    const changeHref = await page.getAttribute(
      'a[aria-label="Change song"]',
      "href",
    );
    if (!changeHref || !changeHref.endsWith("?replace=0")) {
      throw new Error(`Expected Change link to ?replace=0, got ${changeHref}`);
    }

    const nSongSelects = await page.locator(
      'select[name="song_0"]',
    ).count();
    if (nSongSelects !== 0) {
      throw new Error("Per-row song <select> should be gone (omnisearch picker)");
    }

    // POST edit form via fetch with the rendered row contract
    const formTitle = await page.inputValue('input[name="title"]');
    const cookies2 = await page.context().cookies();
    const csrfCookie2 = cookies2.find((c) => c.name === "csrf_token");
    if (!csrfCookie2) throw new Error("csrf_token cookie not found after loading edit page");
    const editResp = await page.evaluate(
      async (
        {
          url,
          csrfToken,
          songId,
          titleVal,
        }: { url: string; csrfToken: string; songId: string; titleVal: string },
      ) => {
        const fd = new FormData();
        fd.append("title", titleVal);
        fd.append("amount", "1");
        fd.append("song_0", songId);
        fd.append("key_0", "0");
        fd.append("fmt_0", "any");
        fd.append("csrf_token", csrfToken);
        const r = await fetch(url, { method: "POST", body: fd });
        return { status: r.status, location: r.url };
      },
      { url: `${BASE}/gig/${sbId}/edit`, csrfToken: csrfCookie2.value, songId: SEED_SONG_ID, titleVal: formTitle },
    );

    if (editResp.status !== 303 && editResp.status !== 200) {
      throw new Error(
        `Gig edit POST returned unexpected status ${editResp.status}`,
      );
    }

    // ── 4. View page still shows the title and the saved song ──────────────
    await page.goto(`${BASE}/gig/${sbId}`);
    await page.waitForSelector("body", { timeout: 5000 });
    await waitForText(page, "body", sbTitle);
    await waitForText(page, "body", SEED_SONG_TITLE);
  } finally {
    // Cleanup: remove the created gig directory
    if (sbId) {
      try {
        const sbPath = `${REPO_ROOT}/var/gig/${sbId}`;
        for await (const entry of Deno.readDir(sbPath)) {
          await Deno.remove(`${sbPath}/${entry.name}`);
        }
        await Deno.remove(sbPath);
      } catch {
        // ignore cleanup errors
      }
    }
    await browser.close();
  }
});
