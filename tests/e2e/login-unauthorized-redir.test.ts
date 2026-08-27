/**
 * E2E test: unauthorized access redirects to login with ?ret= preservation
 *
 * Tests:
 *   1. Accessing a protected edit page (e.g. /song/nonexistent_or_test/edit or /song/new/) without login
 *      returns a 401 with a login form containing hidden ret input matching the requested URI.
 *   2. Logging in redirects back to the protected URL.
 */

import { chromium } from "npm:playwright";
import { registerUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test({
  name: "auth: unauthorized page access sets ?ret= and redirects back after login",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await page.route("**/_frsh/js/**", (route) => route.abort());
    await page.route("**/styles.css", (route) => route.abort());

    const GOTO = { waitUntil: "domcontentloaded" as const };

    // Register a user first
    const user = {
      username: `e2e_${crypto.randomUUID().replace(/-/g, "").slice(0, 16)}`,
      password: `pw_${crypto.randomUUID().replace(/-/g, "").slice(0, 16)}`,
    };
    await registerUser(BASE, user);

    // ── 1. Load protected page without being logged in ────────────────────────
    // /song/test_item/edit or /poem/test_item/edit
    const targetPath = "/song/test_item/edit";
    const response = await page.goto(`${BASE}${targetPath}`, GOTO);

    // Should receive 401 status
    if (response && response.status() !== 401) {
      throw new Error(`Expected HTTP 401 on unauthorized page, got ${response.status()}`);
    }

    await page.waitForSelector('input[name="ret"]', { state: "attached", timeout: 5000 });

    const retValue = await page.inputValue('input[name="ret"]');
    if (retValue !== targetPath) {
      throw new Error(
        `Expected hidden ret input to be "${targetPath}", got "${retValue}"`,
      );
    }

    // ── 2. Submit login and verify redirect target is targetPath ──────────────
    await page.fill('input[name="username"]', user.username);
    await page.fill('input[name="password"]', user.password);
    await page.click('form[method="POST"] button[type="submit"]');

    await page.waitForURL((url) => url.pathname === targetPath, { timeout: 8000 });
  } finally {
    await browser.close();
  }
});
