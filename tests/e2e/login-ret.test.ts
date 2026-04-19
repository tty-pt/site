/**
 * E2E test: login ?ret= redirect
 *
 * Tests:
 *   1. GET /auth/login?ret=/song/ — hidden ret input has value "/song/"
 *   2. Logging in redirects to /song/, not /
 *
 * Requires: NDC (8080), Fresh (3000) running.
 */

import { chromium } from "npm:playwright";
import { registerUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test({
  name: "login: ?ret= param is forwarded into hidden field and used as redirect target",
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

    // Register a user (no browser confirm needed — loginUser confirms via log)
    const user = {
      username: `e2e_${crypto.randomUUID().replace(/-/g, "").slice(0, 16)}`,
      password: `pw_${crypto.randomUUID().replace(/-/g, "").slice(0, 16)}`,
    };
    await registerUser(BASE, user);

    // ── 1. Load login page with ?ret=/song/ ──────────────────────────────────
    await page.goto(`${BASE}/auth/login?ret=/song/`, GOTO);
    await page.waitForSelector('input[name="ret"]', { state: "attached", timeout: 5000 });

    const retValue = await page.inputValue('input[name="ret"]');
    if (retValue !== "/song/") {
      throw new Error(
        `Expected hidden ret input to be "/song/", got "${retValue}"`,
      );
    }

    // ── 2. Submit login and verify redirect target is /song/ ─────────────────
    await page.fill('input[name="username"]', user.username);
    await page.fill('input[name="password"]', user.password);
    await page.click('button[type="submit"]');

    await page.waitForURL(/\/song\/?$/, { timeout: 8000 });
  } finally {
    await browser.close();
  }
});
