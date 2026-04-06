/**
 * E2E test: song edit happy path
 *
 * Requires both servers to be running:
 *   NDC on port 8080  (./start.sh)
 *   Fresh on port 3000 (deno task start)
 */

import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";
const SONG_ID = "abba_part_frei_gilson";
const ORIGINAL_TITLE = "Abba (part. Frei Gilson)";
const EDITED_TITLE = "Abba (part. Frei Gilson) [E2E EDIT]";
const LOG_FILE = "/tmp/site.log";

async function getConfirmUrl(username: string): Promise<string> {
  // Poll the NDC log for the registration confirmation URL (max 5s)
  const deadline = Date.now() + 5000;
  const pattern = new RegExp(
    `Register: (/auth/confirm\\?u=${username}&r=[a-f0-9]+)`,
  );
  while (Date.now() < deadline) {
    const log = await Deno.readTextFile(LOG_FILE);
    const lines = log.split("\n").reverse();
    for (const line of lines) {
      const m = line.match(pattern);
      if (m) return m[1];
    }
    await new Promise((r) => setTimeout(r, 200));
  }
  throw new Error(`Confirmation URL for ${username} not found in ${LOG_FILE}`);
}

Deno.test("song edit: login → edit title → verify on detail page", async () => {
  const username = `e2etest_${Date.now()}`;
  const password = "pw_e2etest_1";

  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    // ── 1. Register ──────────────────────────────────────────────────────────
    const regResp = await fetch(`${BASE}/auth/register`, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({
        username,
        password,
        password2: password,
        email: `${username}@example.com`,
      }).toString(),
      redirect: "manual",
    });
    if (regResp.status !== 303) {
      throw new Error(`Register expected 303, got ${regResp.status}`);
    }
    await regResp.body?.cancel();

    // ── 2. Confirm ───────────────────────────────────────────────────────────
    const confirmPath = await getConfirmUrl(username);
    const confResp = await fetch(`${BASE}${confirmPath}`, {
      redirect: "manual",
    });
    if (confResp.status !== 303) {
      throw new Error(`Confirm expected 303, got ${confResp.status}`);
    }
    await confResp.body?.cancel();

    // ── 3. Login via browser page (sets cookie in browser context) ───────────
    await page.goto(`${BASE}/auth/login`);
    await page.fill('input[name="username"]', username);
    await page.fill('input[name="password"]', password);
    await page.click('button[type="submit"]');
    // Should redirect to / after login
    await page.waitForURL(`${BASE}/`, { timeout: 5000 });

    // ── 4. Navigate to song edit page ─────────────────────────────────────────
    await page.goto(`${BASE}/song/${SONG_ID}/edit`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });

    // Verify the form is pre-populated with the original title
    const currentTitle = await page.inputValue('input[name="title"]');
    if (currentTitle !== ORIGINAL_TITLE) {
      throw new Error(
        `Expected pre-filled title "${ORIGINAL_TITLE}", got "${currentTitle}"`,
      );
    }

    // ── 5. Edit the title and submit ──────────────────────────────────────────
    await page.fill('input[name="title"]', EDITED_TITLE);
    await page.click('button[type="submit"]');

    // Should redirect to /song/:id after save
    await page.waitForURL(`${BASE}/song/${SONG_ID}`, { timeout: 5000 });

    // ── 6. Verify new title appears on the detail page ────────────────────────
    await page.waitForSelector("h1", { timeout: 5000 });
    const h1 = await page.textContent("h1");
    if (!h1?.includes(EDITED_TITLE)) {
      throw new Error(
        `Detail page h1 does not contain edited title.\nGot: "${h1}"`,
      );
    }

    // ── 7. Restore original title (cleanup) ───────────────────────────────────
    await page.goto(`${BASE}/song/${SONG_ID}/edit`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', ORIGINAL_TITLE);
    await page.click('button[type="submit"]');
    await page.waitForURL(`${BASE}/song/${SONG_ID}`, { timeout: 5000 });

    // Confirm restored
    await page.waitForSelector("h1", { timeout: 5000 });
    const restoredH1 = await page.textContent("h1");
    if (!restoredH1?.includes(ORIGINAL_TITLE)) {
      throw new Error(
        `Title not restored after cleanup.\nGot: "${restoredH1}"`,
      );
    }
  } finally {
    await browser.close();
  }
});
