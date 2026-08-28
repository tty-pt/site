/**
 * E2E test verifying the Group picker on the Gig Edit page lists groups, not songs.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const GOTO = { waitUntil: "domcontentloaded" as const };

function assert(condition: boolean, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

Deno.test({
  name: "gig edit: group picker lists groups, not songs",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const context = await browser.newContext();
    const page = await context.newPage();

    try {
      page.setDefaultNavigationTimeout(15000);
      page.setDefaultTimeout(15000);
      await createAndLoginUser(page, BASE);

      const unique = crypto.randomUUID().slice(0, 8);
      const grpTitle = `Alpha Group ${unique}`;
      const songTitle = `Unique Test Song ${unique}`;

      // 1. Create a song
      await page.goto(`${BASE}/song/add`, GOTO);
      await page.locator('form[method="POST"] input[name="title"]').fill(songTitle);
      await Promise.all([
        page.waitForURL(/\/song\/[^\/]+$/, { timeout: 10000 }),
        page.locator('button[type="submit"]').click(),
      ]);

      // 2. Create a group
      await page.goto(`${BASE}/grp/add`, GOTO);
      await page.locator('form[method="POST"] input[name="title"]').fill(grpTitle);
      await Promise.all([
        page.waitForURL(/\/grp\/[^\/]+$/, { timeout: 10000 }),
        page.locator('button[type="submit"]').click(),
      ]);
      const grpId = page.url().split("/grp/")[1].replace(/\/$/, "");

      // 3. Create a gig
      await page.goto(`${BASE}/gig/add`, GOTO);
      await page.locator('form[method="POST"] input[name="title"]').fill(`Gig For Group Test ${unique}`);
      // Open group picker on gig add and select the group
      await page.locator('.hyle-picker[data-hyle-picker-key="grp"] details.hyle-picker-details summary').click();
      await page.locator('input[name="pick_q_grp"]').fill(grpTitle);
      const addGrpRows = page.locator('.hyle-picker[data-hyle-picker-key="grp"] .hyle-picker-rows').first();
      await addGrpRows.waitFor({ state: "visible" });
      while (!(await addGrpRows.innerText()).includes(grpTitle)) {
        await page.waitForTimeout(300);
      }
      await page.locator(`.hyle-picker[data-hyle-picker-key="grp"] input[value="${grpId}"]`).check({ force: true });

      await Promise.all([
        page.waitForURL(/\/gig\/[^\/]+$/, { timeout: 10000 }),
        page.locator('button[type="submit"]').click(),
      ]);
      const gigId = page.url().split("/gig/")[1].replace(/\/$/, "");

      // 4. Open gig edit page and verify group picker SSR initial options
      await page.goto(`${BASE}/gig/${gigId}/edit`, GOTO);

      const grpPicker = page.locator('.hyle-picker[data-hyle-picker-key="grp"]');
      assert(await grpPicker.count() >= 1, "group picker should be present on gig edit page");

      // Verify the pinned selection in summary/values is our group
      const selectedValue = await page.locator('.hyle-picker[data-hyle-picker-key="grp"] .hyle-picker-values').innerText();
      assert(selectedValue.includes(grpId) || selectedValue.includes(grpTitle), `group picker selected value should be group, got: "${selectedValue}"`);

      await page.locator('.hyle-picker[data-hyle-picker-key="grp"] details.hyle-picker-details summary').click();

      // Check INITIAL LOAD options in group picker before any search input
      const initialOptions = await page.locator('.hyle-picker[data-hyle-picker-key="grp"] .hyle-picker-rows').first().innerText();
      assert(!initialOptions.includes(songTitle), `group picker initial options must NOT include song title "${songTitle}"`);

      // Verify radio values in group picker match groups and not songs
      const radioValues = await page.locator('.hyle-picker[data-hyle-picker-key="grp"] input[type="radio"]').evaluateAll(
        (inputs) => inputs.map((i: any) => i.value)
      );
      assert(radioValues.includes(grpId), `radio values must include group ID "${grpId}"`);

      // 5. Search for our group in the group picker
      const searchBox = page.locator('input[name="pick_q_grp"]');
      await searchBox.fill(grpTitle);

      const rows = page.locator('.hyle-picker[data-hyle-picker-key="grp"] .hyle-picker-rows').first();
      await rows.waitFor({ state: "visible" });
      let text = await rows.innerText();
      while (!text.includes(grpTitle)) {
        await page.waitForTimeout(400);
        text = await rows.innerText();
      }

      await context.close();
    } finally {
      await browser.close();
    }
  },
});
