/**
 * E2E test: song add with a title longer than the 256-byte stack buffer
 *
 * Exercises index.c:126/130 directly: mpfd_get copies min(len, 255)
 * bytes into title[256] but returns the FULL field length (e.g. 300),
 * and index_add_item passes that 300 as title_len to axil_slugify →
 * iconv reads past the stack buffer (ASAN-proven).
 *
 * The generated slug will be truncated to the first 255 bytes. The test
 * only asserts the server stays alive and returns a redirect — the
 * precise memory bug is covered deterministically by
 * tests/unit/caller_contract_test.c.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

function longTitle(bytes: number): string {
  // Accented-heavy filler so bytes == chars here; fine for >255 test.
  return "título ".repeat(Math.ceil(bytes / 7)).slice(0, bytes);
}

Deno.test("song: title longer than 256 bytes does not kill the server", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  const title = longTitle(300);
  let slug: string | null = null;

  try {
    await createAndLoginUser(page, BASE);

    await page.goto(`${BASE}/song/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', title);
    await page.click('button[type="submit"]');

    await page.waitForURL(/\/song\/[^/]+$/, { timeout: 5000 });
    slug = page.url().split("/song/")[1]?.replace(/\/$/, "");
    if (!slug) throw new Error(`no slug in URL: ${page.url()}`);

    const health = await fetch(`${BASE}/`);
    if (health.status !== 200) {
      throw new Error(`Server did not respond 200 after long-title add (${health.status})`);
    }
    await health.body?.cancel();
  } finally {
    await browser.close();
    if (slug) {
      try {
        await Deno.remove(`items/song/items/${slug}`, { recursive: true });
      } catch {
        // already gone
      }
    }
  }
});
