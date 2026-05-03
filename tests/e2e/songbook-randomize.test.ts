/**
 * E2E test: songbook randomize
 *
 * Creates a songbook, seeds it with one song entry, then clicks the
 * randomize button on the detail page and verifies the page stays on
 * the songbook detail (the randomize POST redirects back to the same page).
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
// A song that exists in the fixture data
const SEED_SONG_ID = "abba_part_frei_gilson";

Deno.test("songbook randomize: create songbook → seed data → click randomize → stays on page", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  let sbId: string | null = null;

  try {
    await createAndLoginUser(page, BASE);
    const sbTitle = `Randomize Test ${Date.now()}`;

    // ── 1. Create songbook ────────────────────────────────────────────────────
    await page.goto(`${BASE}/songbook/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', sbTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/songbook\/[^/]+$/, { timeout: 5000 });

    const sbUrl = page.url();
    sbId = sbUrl.split("/songbook/")[1];
    if (!sbId) throw new Error(`Could not extract songbook id from ${sbUrl}`);

    // ── 2. Seed data via POST to edit endpoint ────────────────────────────────
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    const { token: csrfToken, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);
    const formData = new FormData();
    formData.set("amount", "1");
    formData.set("song_0", SEED_SONG_ID);
    formData.set("fmt_0", "any");
    formData.set("t_0", "0");
    formData.set("csrf_token", csrfToken);

    const editRes = await fetch(`${BASE}/songbook/${sbId}/edit`, {
      method: "POST",
      headers: { "Cookie": ch },
      body: formData,
      redirect: "manual",
    });
    if (editRes.status !== 303 && editRes.status !== 200) {
      await editRes.text();
      throw new Error(`Failed to seed songbook data: HTTP ${editRes.status}`);
    }
    await editRes.body?.cancel();

    // ── 3. Navigate to songbook detail page ───────────────────────────────────
    await page.goto(`${BASE}/songbook/${sbId}`);
    await page.waitForSelector('form[action$="/randomize"] button[type="submit"]', { timeout: 8000 });

    // ── 4. Click the randomize (🎲) button ────────────────────────────────────
    // The randomize POST redirects to /songbook/:id#<n> — a fragment-only
    // navigation that doesn't trigger waitForURL, so we wait on the response.
    const [randomizeRes] = await Promise.all([
      page.waitForResponse((r) => r.url().includes("/randomize")),
      page.click('form[action$="/randomize"] button[type="submit"]'),
    ]);
    if (randomizeRes.status() >= 400) {
      const body = await randomizeRes.text();
      throw new Error(`Randomize returned HTTP ${randomizeRes.status()}: ${body}`);
    }

    // ── 5. Verify the page still renders the songbook ─────────────────────────
    await page.waitForSelector("h1", { timeout: 5000 });
    const h1Text = await page.textContent("h1");
    if (!h1Text?.includes(sbTitle)) {
      throw new Error(
        `After randomize, <h1> does not contain songbook title "${sbTitle}", got: "${h1Text}"`,
      );
    }
  } finally {
    await browser.close();
  }
});
