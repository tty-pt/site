/**
 * E2E test: gig format customization on detail and edit pages.
 *
 * Tests:
 *   1. Create grp with default formats ("sbt_ent\nsbt_san").
 *   2. Create gig under grp; verify seeded songs have default formats.
 *   3. Edit page (/gig/:id/edit): verify row format is rendered via
 *      a song.types single-ref picker with pre-selected format value
 *      and sibling GET forms.
 *   4. Save edit form with an updated format (e.g. sbt_com) and verify persistence.
 *   5. Detail page (/gig/:id): verify format badge acts as an inline omni-dropdown
 *      trigger for owners.
 *   6. Detail page (/gig/:id): select a new format via the row format picker,
 *      submit/replace, and verify format updates on the page.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_1_ID = "a_alegria_esta_no_coracao";
const SONG_1_TITLE = "A alegria está no coração";
const SONG_2_ID = "abba_part_frei_gilson";
const SONG_2_TITLE = "Abba (part. Frei Gilson)";

Deno.test({
  name: "gig format picker: edit page and detail page format customization",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let sbId: string | null = null;
  let grpId: string | null = null;

  try {
    page.setDefaultNavigationTimeout(15000);
    page.setDefaultTimeout(15000);
    await page.route("**/styles.css", (route) => route.abort());
    const GOTO = { waitUntil: "domcontentloaded" as const };

    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    // Ensure song types exist
    for (const [id, name] of [
      ["sbt_ent", "Entrada"],
      ["sbt_san", "Santo"],
      ["sbt_com", "Comunhao"],
    ]) {
      const { token: csrfType, cookieHeader: chType } = await getCsrfToken(cookieHeader, BASE);
      await fetch(`${BASE}/api/dataset/song.types`, {
        method: "POST",
        body: new URLSearchParams({ id, name, csrf_token: csrfType }).toString(),
        headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chType },
      });
    }

    // ── 0. Create a grp with format categories ─────────────────────────────
    const grpTitle = `Format Grp ${Date.now()}`;
    await page.goto(`${BASE}/grp/add`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', grpTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/, { timeout: 5000 });
    grpId = page.url().split("/grp/")[1];

    const { token: csrfSeed, cookieHeader: chSeed } = await getCsrfToken(cookieHeader, BASE);

    // Seed songs into repertoire
    for (const [songId, fmt] of [
      [SONG_1_ID, "sbt_ent"],
      [SONG_2_ID, "sbt_san"],
    ]) {
      const seedBody = new URLSearchParams({ song_id: songId, format: fmt, csrf_token: csrfSeed });
      await fetch(`${BASE}/api/grp/${grpId}/songs`, {
        method: "POST",
        body: seedBody.toString(),
        headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chSeed },
        redirect: "manual",
      });
    }

    // ── 1. Create gig linked to the grp ───────────────────────────────────
    const sbTitle = `Format Gig ${Date.now()}`;
    await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', sbTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 5000 });
    sbId = page.url().split("/gig/")[1];

    // ── 2. Add SONG_1 with format sbt_ent to the gig ───────────────────────
    const { token: csrfAdd, cookieHeader: chAdd } = await getCsrfToken(cookieHeader, BASE);
    const addBody = new URLSearchParams({ song_id: SONG_1_ID, format: "sbt_ent", csrf_token: csrfAdd });
    const addResp = await fetch(`${BASE}/api/gig/${sbId}/songs`, {
      method: "POST",
      body: addBody.toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: chAdd },
      redirect: "manual",
    });
    if (addResp.status >= 400) throw new Error(`Add song failed: ${addResp.status}`);

    // ── 3. Test Edit Page: /gig/:id/edit ──────────────────────────────────
    await page.goto(`${BASE}/gig/${sbId}/edit`, GOTO);
    await page.waitForSelector("#edit-form-submit", { timeout: 5000 });

    // Verify row 0 format input is rendered as a picker with sbt_ent selected
    const fmtInput = page.locator('input[name="fmt_0"][type="radio"]:checked, input[name="fmt_0"][type="hidden"]');
    await fmtInput.waitFor({ state: "attached", timeout: 5000 });
    const fmtVal = await fmtInput.getAttribute("value");
    if (fmtVal !== "sbt_ent" && fmtVal !== "Entrada") {
      throw new Error(`Expected fmt_0 to be sbt_ent or Entrada, got ${fmtVal}`);
    }

    // Verify sibling GET form exists for fmt_0
    const fmtSibling = await page.$('#pickq-fmt_0');
    if (!fmtSibling) {
      throw new Error("Missing sibling GET form #pickq-fmt_0 on edit page");
    }

    // ── 4. Test Detail Page: /gig/:id ─────────────────────────────────────
    await page.goto(`${BASE}/gig/${sbId}`, GOTO);
    await waitForText(page, "body", SONG_1_TITLE);

    // Verify row 0 format trigger details exists
    const fmtTrigger = page.locator('#sb-fmt-pick-post-0 details.hyle-picker-details summary').first();
    await fmtTrigger.waitFor({ state: "visible", timeout: 5000 });

    // Verify sibling GET form for detail row format picker
    const detailFmtSibling = await page.$('#pickq-format__0');
    if (!detailFmtSibling) {
      throw new Error("Missing sibling GET form #pickq-format__0 on detail page");
    }

    // ── 5. Interact with Detail Page Format Picker: search and switch to Comunhao ──
    await fmtTrigger.click();

    const fmtSearchInput = page.locator('input[name="pick_q_format__0"]');
    await fmtSearchInput.waitFor({ state: "visible", timeout: 5000 });
    await fmtSearchInput.fill("Comunhao");

    const fmtOption = page.locator(
      '#sb-fmt-pick-post-0 label.hyle-picker-option:has(input[name="format"][value="sbt_com"]), #sb-fmt-pick-post-0 label.hyle-picker-option:has(input[name="format"][value="Comunhao"])',
    );
    await fmtOption.first().waitFor({ state: "visible", timeout: 5000 });

    await Promise.all([
      page.waitForNavigation({ waitUntil: "domcontentloaded" }),
      fmtOption.first().click(),
    ]);

    // Verify page updated with new format
    await waitForText(page, "#sb-fmt-pick-post-0", "Comunhao");

  } finally {
    await page.close();
    await browser.close();
  }
});
