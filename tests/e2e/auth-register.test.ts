/**
 * E2E test: full browser registration flow
 *
 * Tests:
 *   1. Navigate to /auth/register in browser
 *   2. Fill all fields and submit
 *   3. Confirm registration via link found in server log
 *   4. Verify confirmation redirects successfully
 *
 * Requires: NDC (8080), Fresh (3000) running.
 */

import { chromium } from "npm:playwright";
import { confirmUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("auth: browser register → confirm → can log in", async () => {
  const username = `e2e_reg_${Date.now()}`;
  const password = `pw_${Date.now()}`;

  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    // ── 1. Navigate to register page ────────────────────────────────────────
    await page.goto(`${BASE}/auth/register`);
    await page.waitForSelector('input[name="username"]', { timeout: 5000 });

    // ── 2. Fill the registration form ────────────────────────────────────────
    await page.fill('input[name="username"]', username);
    await page.fill('input[name="password"]', password);
    await page.fill('input[name="password2"]', password);
    await page.fill('input[name="email"]', `${username}@example.com`);
    await page.click('button[type="submit"]');

    // After submitting, the server returns a 303 redirect. The browser follows
    // it. We just need the page to settle (no error page).
    await page.waitForLoadState("networkidle", { timeout: 8000 });

    const status = page.url();
    if (status.includes("/error") || status.includes("/500")) {
      throw new Error(`Register redirected to unexpected URL: ${status}`);
    }

    // ── 3. Confirm via link from server log ─────────────────────────────────
    await confirmUser(BASE, username);

    // ── 4. Now log in with the confirmed account ────────────────────────────
    await page.goto(`${BASE}/auth/login`);
    await page.waitForSelector('input[name="username"]', { timeout: 5000 });
    await page.fill('input[name="username"]', username);
    await page.fill('input[name="password"]', password);
    await page.click('button[type="submit"]');
    await page.waitForURL(`${BASE}/`, { timeout: 5000 });
  } finally {
    await browser.close();
  }
});
