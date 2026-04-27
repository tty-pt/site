/**
 * E2E test: poem add happy path
 *
 * Tests:
 *   1. Authenticated user can create a poem via /poem/add (title only)
 *   2. After submission, redirects to /poem/<id> detail page
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("poem: login → add poem → verify detail page", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  const ts = Date.now();
  const poemTitle = `Test Poem E2E ${ts}`;

  try {
    await createAndLoginUser(page, BASE);

    // ── 1. Navigate to /poem/add ──────────────────────────────────────────────
    await page.goto(`${BASE}/poem/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });

    // ── 2. Fill title and submit ──────────────────────────────────────────────
    await page.fill('input[name="title"]', poemTitle);
    await page.click('button[type="submit"]');

    // Should redirect to /poem/<id> after creation
    await page.waitForURL(`${BASE}/poem/**`, { timeout: 5000 });

    // ── 3. Verify detail page loads ───────────────────────────────────────────
    await page.waitForSelector("body", { timeout: 5000 });
    const url = page.url();
    if (!url.includes("/poem/")) {
      throw new Error(`Expected /poem/<id> URL, got: ${url}`);
    }
  } finally {
    const url = page.url();
    const poemId = url.replace(`${BASE}/poem/`, "").replace(/\/$/, "");
    await browser.close();

    // Cleanup: remove created poem directory
    if (poemId) {
      try {
        await Deno.remove(`items/poem/items/${poemId}`, { recursive: true });
      } catch {
        // Already gone or never created — ignore
      }
    }
  }
});
