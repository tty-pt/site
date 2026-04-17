/**
 * E2E test: poem delete happy path
 *
 * Tests:
 *   1. Authenticated user can add a poem
 *   2. Navigate to /poem/<id>/delete, verify confirmation page
 *   3. Submit deletion form
 *   4. Poem is gone (detail page returns 404 or poem listing no longer has it)
 *
 * Requires: NDC (8080), Fresh (3000) running.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("poem delete: login → add poem → delete → verify gone", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  const ts = Date.now();
  const title = `Poem Delete Test ${ts}`;
  let poemId = "";

  try {
    await createAndLoginUser(page, BASE);

    // ── 1. Add poem ───────────────────────────────────────────────────────────
    await page.goto(`${BASE}/poem/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', title);
    await page.click('button[type="submit"]');

    await page.waitForURL(`${BASE}/poem/**`, { timeout: 5000 });
    const url = page.url();
    poemId = url.replace(`${BASE}/poem/`, "").replace(/\/$/, "");

    if (!poemId) {
      throw new Error(`Could not extract poem ID from URL: ${url}`);
    }

    // ── 2. Navigate to delete page ────────────────────────────────────────────
    await page.goto(`${BASE}/poem/${poemId}/delete`);
    await page.waitForSelector('button[type="submit"]', { timeout: 5000 });

    const bodyText = await page.textContent("body");
    if (!bodyText?.includes(title)) {
      throw new Error(
        `Delete confirmation page does not mention poem title.\nExpected: "${title}"\nPage body: "${bodyText?.slice(0, 500)}"`,
      );
    }

    // ── 3. Confirm deletion ───────────────────────────────────────────────────
    await page.click('button[type="submit"]');
    await page.waitForURL(`${BASE}/poem`, { timeout: 5000 });

    // ── 4. Verify poem is gone ────────────────────────────────────────────────
    const resp = await fetch(`${BASE}/poem/${poemId}`, { redirect: "manual" });
    await resp.body?.cancel();
    if (resp.status !== 404) {
      throw new Error(
        `Expected 404 after deletion, got ${resp.status} for /poem/${poemId}`,
      );
    }
  } finally {
    await browser.close();

    // Cleanup in case deletion failed
    if (poemId) {
      try {
        await Deno.remove(`items/poem/items/${poemId}`, { recursive: true });
      } catch {
        // Already gone — ignore
      }
    }
  }
});
