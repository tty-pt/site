/**
 * E2E test: index list page — "add" button placement
 *
 * Tests:
 *   1. Unauthenticated: no "add" button on /song/
 *   2. Authenticated: "add" button is in the sidebar (.functions), not in .center
 *
 * Requires: NDC (8080), Fresh (3000) running.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test({
  name: "index list: add button is in sidebar when logged in, absent when logged out",
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

    // ── 1. Logged out: no add button anywhere ────────────────────────────────
    await page.goto(`${BASE}/song/`, GOTO);
    await page.waitForSelector(".center", { timeout: 5000 });

    const addLinksLoggedOut = await page.$$('a[href="/song/add"]');
    if (addLinksLoggedOut.length !== 0) {
      throw new Error(
        `Expected no "add" link when logged out, found ${addLinksLoggedOut.length}`,
      );
    }

    // ── 2. Log in ─────────────────────────────────────────────────────────────
    await createAndLoginUser(page, BASE);

    await page.goto(`${BASE}/song/`, GOTO);
    await page.waitForSelector(".center", { timeout: 5000 });

    // Must appear somewhere in the sidebar (.functions)
    const addInSidebar = await page.$('.functions a[href="/song/add"]');
    if (!addInSidebar) {
      throw new Error('Expected "add" link inside .functions sidebar when logged in');
    }

    // Must NOT appear inside .center
    const addInCenter = await page.$('.center a[href="/song/add"]');
    if (addInCenter) {
      throw new Error('Expected "add" link NOT inside .center, but it was found there');
    }
  } finally {
    await browser.close();
  }
});
