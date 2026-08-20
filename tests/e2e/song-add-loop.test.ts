/**
 * E2E test: rapid repeated song adds — daemon liveness
 *
 * Adds several songs in quick succession (browser-form driven), then
 * verifies the server still responds 200. This is a regression guard for
 * the live-box crash pattern: a crash mid-add must never leave the
 * daemon down, and after any crash-before-fix the subsequent adds here
 * would fail (connection refused) rather than 303.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const N = 5;

Deno.test("song: N rapid adds leave the daemon alive", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const created: string[] = [];

  try {
    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    for (let i = 0; i < N; i++) {
      const title = `Loop Song ${Date.now()}_${i}`;
      await page.goto(`${BASE}/song/add`);
      await page.waitForSelector('input[name="title"]', { timeout: 5000 });
      await page.fill('input[name="title"]', title);
      await page.click('button[type="submit"]');
      await page.waitForURL(/\/song\/[^/]+$/, { timeout: 5000 });
      created.push(page.url().split("/song/")[1]?.replace(/\/$/, ""));
    }

    const health = await fetch(`${BASE}/`, {
      headers: { Cookie: cookieHeader },
    });
    if (health.status !== 200) {
      throw new Error(`Server not alive after ${N} adds (${health.status})`);
    }
    await health.body?.cancel();
  } finally {
    await browser.close();
    for (const slug of created) {
      if (!slug) continue;
      try {
        await Deno.remove(`var/song/${slug}`, { recursive: true });
      } catch {
        // already gone
      }
    }
  }
});
