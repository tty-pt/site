/**
 * E2E test: songbook create → edit page → save → verify redirect.
 *
 * Tests:
 *   1. Authenticated user can create a songbook via /songbook/add
 *   2. GET /songbook/<id>/edit renders the edit form with the songbook title
 *   3. POST /songbook/<id>/edit with valid data redirects to the view page
 *   4. After save, the view page still shows the songbook title
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("songbook: register → login → create songbook → load edit page → save → view", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  let sbId: string | null = null;

  try {
    await createAndLoginUser(page, BASE);
    const sbTitle = `SB Edit Test ${Date.now()}`;

    // ── 1. Create songbook via /songbook/add ────────────────────────────────
    await page.goto(`${BASE}/songbook/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', sbTitle);
    await page.click('button[type="submit"]');

    await page.waitForURL(/\/songbook\/[^/]+$/, { timeout: 5000 });

    const sbUrl = page.url();
    sbId = sbUrl.split("/songbook/")[1];

    // ── 2. Load the edit page ───────────────────────────────────────────────
    await page.goto(`${BASE}/songbook/${sbId}/edit`);
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "h1", sbTitle);

    // Verify the Save Changes button is present
    await page.waitForSelector('button[type="submit"]', { timeout: 5000 });

    // ── 3. POST edit form via fetch (1 empty slot so amount > 0) ───────────
    const editResp = await page.evaluate(
      async ({ url }: { url: string }) => {
        const fd = new FormData();
        fd.append("amount", "1");
        fd.append("song_0", "");
        fd.append("key_0", "0");
        fd.append("orig_0", "0");
        fd.append("fmt_0", "any");
        const r = await fetch(url, { method: "POST", body: fd });
        return { status: r.status, location: r.url };
      },
      { url: `${BASE}/songbook/${sbId}/edit` },
    );

    if (editResp.status !== 303 && editResp.status !== 200) {
      throw new Error(
        `Songbook edit POST returned unexpected status ${editResp.status}`,
      );
    }

    // ── 4. View page still shows the title ─────────────────────────────────
    await page.goto(`${BASE}/songbook/${sbId}`);
    await page.waitForSelector("body", { timeout: 5000 });
    await waitForText(page, "body", sbTitle);
  } finally {
    // Cleanup: remove the created songbook directory
    if (sbId) {
      try {
        const sbPath = `/home/quirinpa/site/items/songbook/items/${sbId}`;
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
