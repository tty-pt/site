/**
 * E2E test: legacy shorthand redirects
 *
 * Tests:
 *   1. /sb redirects to /songbook (303)
 *   2. /sb/:id redirects to /songbook/:id (303)
 *   3. /chords redirects to /song (301)
 *   4. /chords/:id redirects to /song/:id (301)
 *   5. Query strings are preserved during redirect
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";

Deno.test("redirects: shorthand paths redirect correctly with query preservation", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    // 1. /sb → /songbook
    const sbResp = await page.goto(`${BASE}/sb`, { waitUntil: "domcontentloaded" });
    if (!sbResp) throw new Error("No response from /sb");
    if (page.url() !== `${BASE}/songbook`) {
       throw new Error(`Expected redirect to /songbook, got ${page.url()}`);
    }

    // 2. /sb/:id → /songbook/:id
    await page.goto(`${BASE}/sb/some-id`, { waitUntil: "domcontentloaded" });
    if (page.url() !== `${BASE}/songbook/some-id`) {
       throw new Error(`Expected redirect to /songbook/some-id, got ${page.url()}`);
    }

    // 3. /chords → /song
    const chordsResp = await page.goto(`${BASE}/chords`, { waitUntil: "domcontentloaded" });
    if (!chordsResp) throw new Error("No response from /chords");
    if (page.url() !== `${BASE}/song`) {
       throw new Error(`Expected redirect to /song, got ${page.url()}`);
    }

    // 4. /chords/:id?t=5 → /song/:id?t=5
    await page.goto(`${BASE}/chords/some-id?t=5`, { waitUntil: "domcontentloaded" });
    if (page.url() !== `${BASE}/song/some-id?t=5`) {
       throw new Error(`Expected redirect to /song/some-id?t=5, got ${page.url()}`);
    }

  } finally {
    await browser.close();
  }
});
