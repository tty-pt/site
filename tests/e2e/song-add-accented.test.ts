/**
 * E2E test: song add with accented (non-ASCII) title
 *
 * This reproduces the LIVE tty.pt crash trigger: an authenticated user
 * POSTs a song whose title contains accented characters (e.g. "pão",
 * "Alegria está no coração"). On the live OpenBSD server this crashed
 * the daemon (5/5 times) after the Aug 15 11:14 deploy. Here we verify
 * the flow works and the server stays up (returns 200 on the detail
 * page afterwards).
 *
 * NOTE: locally (glibc) the crash does not reproduce (OpenBSD-only).
 * This test is the e2e guard that MUST pass once the root cause
 * (mpfd_get truncated-copy length + axil_slugify over-read) is fixed,
 * and documents the accented-input path for the live box.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_TITLE = "Alegria está no coração, pão e água";
const expectedSlug = "alegria_esta_no_coracao_pao_e_agua";

Deno.test("song: accented title add does not crash the server", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);

    await page.goto(`${BASE}/song/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', SONG_TITLE);
    await page.click('button[type="submit"]');

    await page.waitForURL(`${BASE}/song/${expectedSlug}`, { timeout: 5000 });

    // Server must still be alive right after.
    const health = await fetch(`${BASE}/`);
    if (health.status !== 200) {
      throw new Error(`Server did not respond 200 after accented add (${health.status})`);
    }
    await health.body?.cancel();
  } finally {
    await browser.close();
    try {
      await Deno.remove(`items/song/items/${expectedSlug}`, { recursive: true });
    } catch {
      // already gone
    }
  }
});
