/**
 * E2E test: full browser registration flow
 *
 * Tests:
 *   1. Navigate to /auth/register in browser
 *   2. Fill all fields and submit
 *   3. Verify registration redirects successfully (users are auto-activated)
 *   4. Log in with the new account
 *
 * Requires: NDC (8080), Fresh (3000) running.
 */

import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";

Deno.test("auth: browser register → can log in", async () => {
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

    // ── 3. Log in with the registered account (users are auto-activated) ────
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
