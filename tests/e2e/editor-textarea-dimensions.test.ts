/**
 * E2E test: Verify that when editing a poem or a song, or adding a song,
 * the contents textbox is big (spacious height) and fits the content area horizontally.
 */

import { chromium } from "npm:playwright";
import { assert } from "jsr:@std/assert";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("editor textareas: song add/edit and poem edit are big and fit horizontally", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  const ts = Date.now();
  const songTitle = `Textarea Song Test ${ts}`;
  const songLyrics = "C   G\nAmazing grace, how sweet the sound\nF   C\nThat saved a wretch like me";
  let songId = "";

  const poemTitle = `Textarea Poem Test ${ts}`;
  const poemLyrics = "<p>First line of the poem</p>\n<p>Second line of the poem</p>";
  let poemId = "";

  try {
    await createAndLoginUser(page, BASE);

    // ── 1. Song Add (/song/add) ──────────────────────────────────────────────
    await page.goto(`${BASE}/song/add`);
    await page.waitForSelector('form[action="/song/add"][method="POST"]', { timeout: 5000 });

    const songAddTextarea = page.locator('textarea[name="data"]');
    await songAddTextarea.waitFor({ state: "visible", timeout: 5000 });

    const formBox = await page.locator('form[action="/song/add"][method="POST"]').boundingBox();
    const songAddBox = await songAddTextarea.boundingBox();

    assert(formBox && songAddBox, "Bounding boxes must exist for song add form and textarea");
    // Should be big (height >= 300px)
    assert(songAddBox.height >= 300, `Song add textarea height should be >= 300px, got ${songAddBox.height}`);
    // Should fit horizontally (textarea width within 10px of form width)
    assert(
      Math.abs(songAddBox.width - formBox.width) <= 10,
      `Song add textarea width (${songAddBox.width}) should match form width (${formBox.width})`,
    );

    // Submit song
    await page.fill('input[name="title"]', songTitle);
    await page.fill('textarea[name="data"]', songLyrics);
    await Promise.all([
      page.waitForURL(/\/song\/(?!add$)[^\/]+$/, { timeout: 10000 }),
      page.click('form[action="/song/add"] button[type="submit"]'),
    ]);
    songId = page.url().replace(`${BASE}/song/`, "").replace(/\/$/, "");

    // ── 2. Song Edit (/song/:id/edit) ────────────────────────────────────────
    await page.goto(`${BASE}/song/${songId}/edit`);
    await page.waitForSelector(`form[action="/song/${songId}/edit"][method="POST"]`, { timeout: 5000 });

    const songEditTextarea = page.locator('textarea[name="data"]');
    await songEditTextarea.waitFor({ state: "visible", timeout: 5000 });

    const songEditFormBox = await page.locator(`form[action="/song/${songId}/edit"][method="POST"]`).boundingBox();
    const songEditBox = await songEditTextarea.boundingBox();

    assert(songEditFormBox && songEditBox, "Bounding boxes must exist for song edit form and textarea");
    assert(songEditBox.height >= 300, `Song edit textarea height should be >= 300px, got ${songEditBox.height}`);
    assert(
      Math.abs(songEditBox.width - songEditFormBox.width) <= 10,
      `Song edit textarea width (${songEditBox.width}) should match form width (${songEditFormBox.width})`,
    );

    // Verify content is pre-populated
    const songEditVal = await songEditTextarea.inputValue();
    assert(
      songEditVal.includes("Amazing grace"),
      `Song edit textarea should contain pre-populated lyrics, got: "${songEditVal}"`,
    );

    // ── 3. Poem Add & Edit (/poem/:id/edit) ───────────────────────────────────
    await page.goto(`${BASE}/poem/add`);
    await page.waitForSelector('form[action="/poem/add"][method="POST"]', { timeout: 5000 });
    await page.fill('input[name="title"]', poemTitle);
    await page.fill('textarea[name="body_content"]', poemLyrics);
    await page.click('form[action="/poem/add"][method="POST"] button[type="submit"]');

    await page.waitForURL(`${BASE}/poem/**`, { timeout: 5000 });
    poemId = page.url().replace(`${BASE}/poem/`, "").replace(/\/$/, "");

    // Navigate to poem edit
    await page.goto(`${BASE}/poem/${poemId}/edit`);
    await page.waitForSelector(`form[action="/poem/${poemId}/edit"][method="POST"]`, { timeout: 5000 });

    const poemEditTextarea = page.locator('textarea[name="body_content"]');
    await poemEditTextarea.waitFor({ state: "visible", timeout: 5000 });

    const poemEditFormBox = await page.locator(`form[action="/poem/${poemId}/edit"][method="POST"]`).boundingBox();
    const poemEditBox = await poemEditTextarea.boundingBox();

    assert(poemEditFormBox && poemEditBox, "Bounding boxes must exist for poem edit form and textarea");
    assert(poemEditBox.height >= 300, `Poem edit textarea height should be >= 300px, got ${poemEditBox.height}`);
    assert(
      Math.abs(poemEditBox.width - poemEditFormBox.width) <= 10,
      `Poem edit textarea width (${poemEditBox.width}) should match form width (${poemEditFormBox.width})`,
    );

    // Verify content is pre-populated
    const poemEditVal = await poemEditTextarea.inputValue();
    assert(
      poemEditVal.includes("First line of the poem"),
      `Poem edit textarea should contain pre-populated poem, got: "${poemEditVal}"`,
    );

  } finally {
    await browser.close();

    if (songId) {
      try {
        await Deno.remove(`var/song/${songId}`, { recursive: true });
      } catch {}
    }
    if (poemId) {
      try {
        await Deno.remove(`var/poem/${poemId}`, { recursive: true });
      } catch {}
    }
  }
});
