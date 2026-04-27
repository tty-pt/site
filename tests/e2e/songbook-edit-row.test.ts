/**
 * E2E test: songbook edit page — edit row client-side enhancement
 *
 * Tests:
 *   1. Edit page renders datalists for types (#types-0) and songs (#songs-0)
 *   2. Key selector (select[name="key_0"]) has 12 options (C–B)
 *   3. Hidden orig field (input[name="orig_0"]) is present
 *   4. Submitting with a known song + key saves correctly and song appears on view page
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const KNOWN_SONG_ID = "a_alegria_esta_no_coracao";
const KNOWN_SONG_TITLE = "A alegria está no coração";

Deno.test({
  name: "songbook edit: client-side row enhancement renders correctly and saves song with key",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  let sbId: string | null = null;

  try {
    page.setDefaultNavigationTimeout(15000);
    page.setDefaultTimeout(15000);

    await page.route("**/styles.css", (route) => route.abort());

    const GOTO = { waitUntil: "domcontentloaded" as const };

    await createAndLoginUser(page, BASE);

    // Create a songbook with 1 slot
    await page.goto(`${BASE}/songbook/add`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', `SB EditRow Test ${Date.now()}`);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/songbook\/[^/]+$/, { timeout: 5000 });
    sbId = page.url().split("/songbook/")[1];

    // ── Load edit page ────────────────────────────────────────────────────────
    await page.goto(`${BASE}/songbook/${sbId}/edit`, GOTO);
    await page.waitForSelector("h1", { timeout: 5000 });

    // New songbook has 0 songs — click "+ Add Row" to append a blank row,
    // which re-renders the page with one edit row.
    await page.click('button[value="add_row"]');
    await page.waitForSelector("h1", { timeout: 5000 });

    // Wait for the client-side enhancement to attach; datalists stay hidden.
    await page.waitForSelector('select[name="key_0"]', { timeout: 8000 });

    // ── 1. Verify datalists exist ─────────────────────────────────────────────
    const typesDatalist = await page.waitForSelector("#types-0", { state: "attached", timeout: 5000 });
    if (!typesDatalist) throw new Error("Expected #types-0 datalist not found");

    const songsDatalist = await page.waitForSelector("#songs-0", { state: "attached", timeout: 5000 });
    if (!songsDatalist) throw new Error("Expected #songs-0 datalist not found");

    // ── 2. Key selector has 12 options ────────────────────────────────────────
    await page.waitForSelector('select[name="key_0"]', { timeout: 5000 });
    const keyOptions = await page.$$('select[name="key_0"] option');
    if (keyOptions.length !== 12) {
      throw new Error(`Expected 12 key options, got ${keyOptions.length}`);
    }

    // Verify first option is "C" and last is "B"
    const firstKey = await keyOptions[0].textContent();
    const lastKey = await keyOptions[11].textContent();
    if (firstKey?.trim() !== "C") throw new Error(`Expected first key "C", got "${firstKey}"`);
    if (lastKey?.trim() !== "B") throw new Error(`Expected last key "B", got "${lastKey}"`);

    // ── 3. Hidden orig field is present ──────────────────────────────────────
    const origField = await page.$('input[name="orig_0"]');
    if (!origField) throw new Error('Expected hidden input[name="orig_0"] not found');

    const origType = await origField.getAttribute("type");
    if (origType !== "hidden") {
      throw new Error(`Expected orig_0 to be type="hidden", got "${origType}"`);
    }

    // ── 4. Fill in a known song + key, submit, verify on view page ────────────
    // Get cookies for out-of-browser API call
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    // We use fetch (multipart) to POST the edit form, simulating the client-side submission.
    // song_0 uses datalist "Title [id]" format; key_0 = 5 (F), orig_0 = 0
    const fd = new FormData();
    fd.append("amount", "1");
    fd.append("song_0", `${KNOWN_SONG_TITLE} [${KNOWN_SONG_ID}]`);
    fd.append("key_0", "5");
    fd.append("orig_0", "0");
    fd.append("fmt_0", "any");

    const editResp = await fetch(`${BASE}/songbook/${sbId}/edit`, {
      method: "POST",
      body: fd,
      headers: { Cookie: cookieHeader },
      redirect: "manual",
    });

    if (editResp.status >= 400) {
      const text = await editResp.text();
      throw new Error(`Songbook edit POST returned ${editResp.status}: ${text.slice(0, 200)}`);
    }
    await editResp.body?.cancel();

    // ── View page should list the song ────────────────────────────────────────
    await page.goto(`${BASE}/songbook/${sbId}`, GOTO);
    await page.waitForSelector("body", { timeout: 5000 });
    await waitForText(page, "body", KNOWN_SONG_TITLE);
  } finally {
    // Cleanup
    if (sbId) {
      try {
        const sbPath = `/home/quirinpa/site/items/songbook/items/${sbId}`;
        for await (const entry of Deno.readDir(sbPath)) {
          await Deno.remove(`${sbPath}/${entry.name}`);
        }
        await Deno.remove(sbPath);
      } catch { /* ignore */ }
    }
    await browser.close();
  }
});
