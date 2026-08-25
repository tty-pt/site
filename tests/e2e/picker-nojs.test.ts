/**
 * E2E test: authenticated song pickers without JavaScript.
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1.
 */

import { chromium, type Page } from "npm:playwright";
import { createAndLoginUser, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const GOTO = { waitUntil: "domcontentloaded" as const };

function assert(condition: boolean, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

async function createSong(
  page: Page,
  title: string,
  type: string,
): Promise<string> {
  await page.goto(`${BASE}/song/add?type=${type}`, GOTO);
  await page.locator('form[method="POST"] input[name="title"]').fill(title);
  await Promise.all([
    page.waitForURL(/\/song\/[^/]+$/, { timeout: 10000 }),
    page.locator('button[type="submit"]').click(),
  ]);
  return page.url().split("/song/")[1].replace(/\/$/, "");
}

async function pickSong(
  page: Page,
  detailPath: string,
  title: string,
  songId: string,
  isOmni: boolean
): Promise<void> {
  if (isOmni) {
    const search = page.locator('input[name="pick_q_song_id"]');
    await page.locator('details.hyle-picker-details summary').first().click();
    await search.fill(title);
    await Promise.all([
      page.waitForNavigation(),
      search.press("Enter")
    ]);

    const rowOpt = page.locator(
      `label.hyle-picker-option:has(input[name="song_id"][value="${songId}"])`,
    );
    await rowOpt.waitFor({ state: "visible", timeout: 10000 });
    assert(
      await page.locator(`input[name="song_id"][value="${songId}"]`).count() === 1,
      `expected exactly one picker option for song ${songId}`,
    );
    await rowOpt.click();

    const addBtn = page.locator('form[method="post"] button.hyle-picker-submit');
    await Promise.all([
      page.waitForURL(`${BASE}${detailPath}`, { timeout: 10000 }),
      addBtn.first().click(),
    ]);
  } else {
    const search = page.locator('form.list-form input[name="q"]');
    await search.fill(title);
    await search.press("Enter");

    const rowAction = page.locator(
      `button.hyle-row-action[name="song_id"][value="${songId}"]`,
    );
    await rowAction.waitFor({ state: "visible", timeout: 10000 });
    assert(
      await rowAction.count() === 1,
      `expected exactly one picker action for song ${songId}`,
    );
    await Promise.all([
      page.waitForURL(`${BASE}${detailPath}`, { timeout: 10000 }),
      rowAction.click(),
    ]);
  }
}

Deno.test({
  name: "pickers: authenticated grp and gig flows work without JavaScript",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const context = await browser.newContext({ javaScriptEnabled: false });
    const page = await context.newPage();

    try {
      page.setDefaultNavigationTimeout(15000);
      page.setDefaultTimeout(15000);
      await createAndLoginUser(page, BASE);

      const unique = crypto.randomUUID().replaceAll("-", "").slice(0, 12);
      const grpSongTitle = `NoJS Communion ${unique}`;
      const gigSongTitle = `NoJS Entry ${unique}`;
      // Use unique type slugs to avoid collision with leftover songs from previous runs (per_page=10 pagination would hide the unique song among many "communion" rows)
      const grpTypeRaw = `communion${unique}`;
      const gigTypeRaw = `entry${unique}`;
      const grpSongId = await createSong(page, grpSongTitle, grpTypeRaw);
      const gigSongId = await createSong(page, gigSongTitle, gigTypeRaw);

      await page.goto(`${BASE}/grp/add`, GOTO);
      await page.locator('form[method="POST"] input[name="title"]').fill(`NoJS Grp ${unique}`);
      await Promise.all([
        page.waitForURL(/\/grp\/[^/]+$/, { timeout: 10000 }),
        page.locator('button[type="submit"]').click(),
      ]);
      const grpId = page.url().split("/grp/")[1].replace(/\/$/, "");

      await pickSong(page, `/grp/${grpId}`, grpSongTitle, grpSongId, false);
      const grpSong = page.locator(`a[href="/grp/${grpId}/song/${grpSongId}"]`);
      await grpSong.waitFor({ state: "visible" });
      const grpRow = grpSong.locator(
        'xpath=ancestor::div[contains(@class, "bg-surface")][1]',
      );
      assert(
        (await grpRow.textContent())?.includes("pinned") ?? false,
        "manual grp picker addition should render as pinned",
      );
      assert(
        await grpRow.locator(
          `form[action="/api/grp/${grpId}/song/${grpSongId}/remove"]`,
        ).count() === 1,
        "manual pinned grp row should have its song-specific remove form",
      );

      await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
      await page.locator('form[method="POST"] input[name="title"]').fill(`NoJS Gig ${unique}`);
      await Promise.all([
        page.waitForURL(/\/gig\/[^/]+$/, { timeout: 10000 }),
        page.locator('button[type="submit"]').click(),
      ]);
      const gigId = page.url().split("/gig/")[1].replace(/\/$/, "");

      await pickSong(page, `/gig/${gigId}`, gigSongTitle, gigSongId, true);
      await waitForText(page, "body", gigSongTitle);
      assert(
        await page.locator(`[data-gig-item] a[href^="/song/${gigSongId}"]`)
          .count() === 1,
        "gig should contain the song selected by its exact picker action",
      );

      await page.goto(`${BASE}/song/?custom=1`, GOTO);
      await page.waitForSelector('tr.hyle-row-clickable', { timeout: 10000 });
      await page.locator(
        'details.hyle-multiselect[data-hyle-ms="type"] summary',
      ).click();
      const typeCheckbox = page.locator(
        `details.hyle-multiselect[data-hyle-ms="type"] input[name="type"][value="${grpTypeRaw}"]`,
      );
      await typeCheckbox.check();
      await Promise.all([
        page.waitForURL((url) =>
          url.searchParams.get("custom") === "1" &&
          url.searchParams.getAll("type").includes(grpTypeRaw)
        ),
        page.locator('.hyle-filter-actions button[type="submit"]').click(),
      ]);

      assert(
        await typeCheckbox.isChecked(),
        "SSR should preserve the type checkbox",
      );
      // song listing rows are <a href="/song/:id"> links, not picker buttons
      assert(
        await page.locator(`a[href="/song/${grpSongId}"]`).count() >= 1,
        "custom type filter should return the matching unique song",
      );
      assert(
        await page.locator(`a[href="/song/${gigSongId}"]`).count() === 0,
        "custom type filter should exclude the other unique song",
      );
    } finally {
      await browser.close();
    }
  },
});
