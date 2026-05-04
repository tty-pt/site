/**
 * E2E test: CSRF token stability across page loads
 *
 * Regression test for: csrf_set_cookie always generated a new token on every
 * ssr_render call, causing the form's hidden csrf_token field to become stale
 * if the browser loaded any other page between the GET /add and the POST /add.
 *
 * Tests:
 *   1. Load /song/add — note the csrf_token cookie value
 *   2. Navigate away to the song list (triggers another ssr_render / new response)
 *   3. Navigate back to /song/add
 *   4. Submit the form — must succeed (303) not fail with 403 Forbidden
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("csrf: token is stable across page loads — add form succeeds after navigating away", async () => {
  const browser = await chromium.launch();
  const context = await browser.newContext();
  const page = await context.newPage();

  const songTitle = "CSRF Stability Test";
  const expectedId = "csrf_stability_test";

  try {
    await createAndLoginUser(page, BASE);

    // 1. Load /song/add and note the csrf cookie
    await page.goto(`${BASE}/song/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    const cookiesBefore = await context.cookies();
    const csrfBefore = cookiesBefore.find((c) => c.name === "csrf_token")?.value;
    if (!csrfBefore) throw new Error("No csrf_token cookie after GET /song/add");

    // 2. Navigate away — this triggers another ssr_render which previously
    //    rotated the csrf_token cookie
    await page.goto(`${BASE}/song/`);
    await page.waitForSelector("body", { timeout: 5000 });

    // 3. Navigate back to /song/add
    await page.goto(`${BASE}/song/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });

    const cookiesAfter = await context.cookies();
    const csrfAfter = cookiesAfter.find((c) => c.name === "csrf_token")?.value;
    if (!csrfAfter) throw new Error("No csrf_token cookie after returning to /song/add");

    // The token must be the same — it should not have rotated
    if (csrfBefore !== csrfAfter) {
      throw new Error(
        `csrf_token rotated between page loads — form will fail.\nBefore: ${csrfBefore}\nAfter:  ${csrfAfter}`,
      );
    }

    // 4. Submit the form — must reach the detail page, not a Forbidden page
    await page.fill('input[name="title"]', songTitle);
    await page.click('button[type="submit"]');
    await page.waitForURL(`${BASE}/song/${expectedId}`, { timeout: 5000 });

    const h1 = await page.textContent("h1");
    if (!h1?.includes(songTitle)) {
      throw new Error(`Expected h1 to include "${songTitle}", got "${h1}"`);
    }
  } finally {
    await browser.close();
    try {
      await Deno.remove(`items/song/items/${expectedId}`, { recursive: true });
    } catch {
      // ignore
    }
  }
});
