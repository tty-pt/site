/**
 * E2E test: auth logout happy path.
 *
 * Tests:
 *   1. Register and login
 *   2. Verify logged in state (has logout link in nav)
 *   3. Logout
 *   4. Verify logged out state (has login link in nav)
 *
 * Requires: ndc running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("auth: login → logout → verify session cleared", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    // Login
    const user = await createAndLoginUser(page, BASE);

    // Verify logged in - has logout link in nav
    await page.goto(`${BASE}/`);
    await page.waitForSelector('a[href="/auth/logout"]', { timeout: 5000 });

    // Logout via /auth/logout
    await page.goto(`${BASE}/auth/logout`);
    await page.waitForURL(`${BASE}/`, { timeout: 5000 });

    // Verify logged out - has login link (and no logout link)
    await page.goto(`${BASE}/`);
    const body = await page.content();

    const hasLogin = body.includes('/auth/login');
    const hasLogout = body.includes('/auth/logout');

    if (!hasLogin) {
      throw new Error("Not logged out - login link not found in nav");
    }
    if (hasLogout) {
      throw new Error("Still logged in - logout link still present after logout");
    }
  } finally {
    await browser.close();
  }
});
