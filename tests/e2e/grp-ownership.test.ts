/**
 * E2E test: grp ownership — non-owner cannot edit
 *
 * Tests:
 *   1. User A creates a grp
 *   2. User B (different account) visits the detail page
 *   3. Edit/Delete menu is NOT shown to user B
 *   4. User B's direct POST to /grp/:id/edit returns 403
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";
import {
  createAndLoginUser,
  logoutUser,
  waitForText,
} from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("grp ownership: non-owner cannot see edit menu or edit grp", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    // ── 1. User A creates a grp ────────────────────────────────────────────
    const userA = await createAndLoginUser(page, BASE);
    const grpTitle = `Ownership Test Grp ${Date.now()}`;

    await page.goto(`${BASE}/grp/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', grpTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/grp\//, { timeout: 5000 });

    const grpUrl = page.url();
    const grpId = grpUrl.split("/grp/")[1];

    // ── 2. Log out user A, register and log in user B ────────────────────────
    await logoutUser(page, BASE);

    await createAndLoginUser(page, BASE);

    // ── 3. User B visits grp detail page ───────────────────────────────────
    await page.goto(`${BASE}/grp/${grpId}`);
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "body", grpTitle);

    // Edit link should NOT be present for user B
    const editLinks = await page.$$('a[href*="/edit"]');
    // Filter to only grp edit links (not unrelated nav links)
    let grpEditVisible = false;
    for (const link of editLinks) {
      const href = await link.getAttribute("href");
      if (href?.includes(`/grp/${grpId}/edit`)) {
        grpEditVisible = true;
        break;
      }
    }
    if (grpEditVisible) {
      throw new Error("Non-owner user B can see the grp edit link — should be hidden");
    }

    // ── 4. User B direct POST to /grp/:id/edit returns 403 ────────────
    const status = await page.evaluate(
      async ({ url }: { url: string }) => {
        const fd = new FormData();
        fd.append("title", "Hacked Title");
        const r = await fetch(url, { method: "POST", body: fd });
        return r.status;
      },
      { url: `${BASE}/grp/${grpId}/edit` },
    );

    if (status !== 403) {
      throw new Error(
        `Expected 403 for non-owner edit attempt, got ${status}`,
      );
    }
  } finally {
    await browser.close();
  }
});
