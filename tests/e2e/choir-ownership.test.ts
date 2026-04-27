/**
 * E2E test: choir ownership — non-owner cannot edit
 *
 * Tests:
 *   1. User A creates a choir
 *   2. User B (different account) visits the detail page
 *   3. Edit/Delete menu is NOT shown to user B
 *   4. User B's direct POST to /api/choir/:id/edit returns 403
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";
import {
  createAndLoginUser,
  logoutUser,
  waitForText,
} from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("choir ownership: non-owner cannot see edit menu or edit choir", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    // ── 1. User A creates a choir ────────────────────────────────────────────
    const userA = await createAndLoginUser(page, BASE);
    const choirTitle = `Ownership Test Choir ${Date.now()}`;

    await page.goto(`${BASE}/choir/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', choirTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(/\/choir\//, { timeout: 5000 });

    const choirUrl = page.url();
    const choirId = choirUrl.split("/choir/")[1];

    // ── 2. Log out user A, register and log in user B ────────────────────────
    await logoutUser(page, BASE);

    await createAndLoginUser(page, BASE);

    // ── 3. User B visits choir detail page ───────────────────────────────────
    await page.goto(`${BASE}/choir/${choirId}`);
    await page.waitForSelector("h1", { timeout: 5000 });
    await waitForText(page, "body", choirTitle);

    // Edit link should NOT be present for user B
    const editLinks = await page.$$('a[href*="/edit"]');
    // Filter to only choir edit links (not unrelated nav links)
    let choirEditVisible = false;
    for (const link of editLinks) {
      const href = await link.getAttribute("href");
      if (href?.includes(`/choir/${choirId}/edit`)) {
        choirEditVisible = true;
        break;
      }
    }
    if (choirEditVisible) {
      throw new Error("Non-owner user B can see the choir edit link — should be hidden");
    }

    // ── 4. User B direct POST to /api/choir/:id/edit returns 403 ────────────
    const status = await page.evaluate(
      async ({ url }: { url: string }) => {
        const fd = new FormData();
        fd.append("title", "Hacked Title");
        const r = await fetch(url, { method: "POST", body: fd });
        return r.status;
      },
      { url: `${BASE}/api/choir/${choirId}/edit` },
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
