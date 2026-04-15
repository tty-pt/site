/**
 * E2E test: poem edit happy path
 *
 * Tests:
 *   1. Authenticated user can add a poem (title only)
 *   2. Navigate to /poem/<id>/edit, verify title is pre-populated
 *   3. Update title and upload an HTML file
 *   4. After submission, redirects to /poem/<id> detail page
 *   5. Detail page shows the uploaded poem content
 *
 * Requires: NDC (8080), Fresh (3000) running.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("poem edit: login → add poem → edit title+file → verify detail page", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  const ts = Date.now();
  const originalTitle = `Poem Edit Test ${ts}`;
  const editedTitle = `${originalTitle} edited`;
  const poemContent = `<p>${editedTitle}</p>`;
  let poemId = "";

  const tmpFile = await Deno.makeTempFile({ suffix: ".html" });
  await Deno.writeTextFile(tmpFile, poemContent);

  try {
    await createAndLoginUser(page, BASE);

    // ── 1. Add poem (title only) ──────────────────────────────────────────────
    await page.goto(`${BASE}/poem/add`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });
    await page.fill('input[name="title"]', originalTitle);
    await page.click('button[type="submit"]');

    await page.waitForURL(`${BASE}/poem/**`, { timeout: 5000 });
    const url = page.url();
    poemId = url.replace(`${BASE}/poem/`, "").replace(/\/$/, "");

    if (!poemId) {
      throw new Error(`Could not extract poem ID from URL: ${url}`);
    }

    // ── 2. Navigate to edit page ──────────────────────────────────────────────
    await page.goto(`${BASE}/poem/${poemId}/edit`);
    await page.waitForSelector('input[name="title"]', { timeout: 5000 });

    // Verify pre-populated title
    const currentTitle = await page.inputValue('input[name="title"]');
    if (currentTitle !== originalTitle) {
      throw new Error(
        `Expected pre-filled title "${originalTitle}", got "${currentTitle}"`,
      );
    }

    // ── 3. Edit title and upload file ─────────────────────────────────────────
    await page.fill('input[name="title"]', editedTitle);
    const fileInput = page.locator('input[type="file"][name="file"]');
    await fileInput.setInputFiles(tmpFile);
    await page.click('button[type="submit"]');

    // Should redirect to /poem/<id> after save
    await page.waitForURL(`${BASE}/poem/${poemId}`, { timeout: 8000 });

    // ── 4. Verify detail page shows uploaded content ──────────────────────────
    await page.waitForSelector("body", { timeout: 5000 });
    const bodyText = await page.textContent("body");
    if (!bodyText?.includes(editedTitle)) {
      throw new Error(
        `Detail page does not contain edited title.\nExpected: "${editedTitle}"\nPage body: "${bodyText?.slice(0, 500)}"`,
      );
    }
  } finally {
    await browser.close();
    await Deno.remove(tmpFile).catch(() => {});

    // Cleanup: remove created poem directory
    if (poemId) {
      try {
        await Deno.remove(`items/poem/items/${poemId}`, { recursive: true });
      } catch {
        // Already gone or never created — ignore
      }
    }
  }
});
