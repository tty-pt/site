import { chromium, type Page } from "npm:playwright";
import { assert } from "jsr:@std/assert";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";
const YT_ID = "uATsjCPK49g";

function uniqueTitle(prefix: string): string {
  return `${prefix} ${crypto.randomUUID().replaceAll("-", "")}`;
}

async function addSong(
  page: Page,
  title: string,
  fields: Record<string, string>,
): Promise<string> {
  await page.goto(`${BASE}/song/add`);
  await page.fill('input[name="title"]', title);
  for (const [name, value] of Object.entries(fields)) {
    await page.fill(`[name="${name}"]`, value);
  }
  await Promise.all([
    page.waitForURL(/\/song\/[^/?]+$/, { timeout: 5000 }),
    page.click('form[method="POST"] button[type="submit"]'),
  ]);
  return new URL(page.url()).pathname.split("/").filter(Boolean).at(-1) ?? "";
}

async function deleteItem(
  page: Page,
  module: "song" | "gig" | "grp",
  id: string,
): Promise<void> {
  await page.goto(`${BASE}/${module}/${id}/delete`);
  const submit = page.locator('button[type="submit"]');
  if (await submit.count()) {
    await Promise.all([
      page.waitForURL(`${BASE}/${module}`),
      submit.click(),
    ]);
  }
}

Deno.test("song media: verify YouTube only song renders YT button and NO ghost PDF button in SSR and WASM", async () => {
  const browser = await chromium.launch();

  // Test SSR (JS disabled)
  {
    const context = await browser.newContext({ javaScriptEnabled: false });
    const page = await context.newPage();
    try {
      await createAndLoginUser(page, BASE);
      await page.goto(`${BASE}/song/${SONG_ID}?m=1`, {
        waitUntil: "domcontentloaded",
      });

      const ytBtn = page.locator(
        `a[href="https://www.youtube.com/watch?v=${YT_ID}"][target="_blank"]`,
      );
      assert((await ytBtn.count()) === 1, "YouTube link button not found in SSR");

      const pdfBtn = page.locator('a[title="View PDF"]');
      assert((await pdfBtn.count()) === 0, "Found ghost PDF button in SSR when song has no PDF");

      const emptyLinks = page.locator('.media-buttons a[href=""]');
      assert((await emptyLinks.count()) === 0, "Found empty href link in media-buttons");
    } finally {
      await context.close();
    }
  }

  // Test WASM (JS enabled)
  {
    const page = await browser.newPage();
    try {
      await createAndLoginUser(page, BASE);
      await page.goto(`${BASE}/song/${SONG_ID}?m=1`, {
        waitUntil: "domcontentloaded",
      });

      const ytBtn = page.locator(
        `a[href="https://www.youtube.com/watch?v=${YT_ID}"][target="_blank"]`,
      );
      assert((await ytBtn.count()) === 1, "YouTube link button not found in WASM");

      const pdfBtn = page.locator('a[title="View PDF"]');
      assert((await pdfBtn.count()) === 0, "Found ghost PDF button in WASM when song has no PDF");
    } finally {
      await page.close();
    }
  }

  await browser.close();
});

Deno.test("song media: verify PDF only song renders PDF button and NO YT button in SSR and WASM", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let songId = "";

  try {
    await createAndLoginUser(page, BASE);
    const pdfUrl = "https://example.com/sheet-music.pdf";
    songId = await addSong(page, uniqueTitle("PDF Only Song"), {
      pdf: pdfUrl,
      data: "C G Am F\nChords line",
    });

    // Verify SSR
    const contextSSR = await browser.newContext({ javaScriptEnabled: false });
    const pageSSR = await contextSSR.newPage();
    await createAndLoginUser(pageSSR, BASE);
    await pageSSR.goto(`${BASE}/song/${songId}?m=1`, {
      waitUntil: "domcontentloaded",
    });

    const pdfBtnSSR = pageSSR.locator(`a[href="${pdfUrl}"][target="_blank"]`);
    assert((await pdfBtnSSR.count()) === 1, "PDF link button not found in SSR");

    const ytBtnSSR = pageSSR.locator('a[title="Watch on YouTube"]');
    assert((await ytBtnSSR.count()) === 0, "Found ghost YouTube button in SSR when song has no YT");
    await contextSSR.close();

    // Verify WASM
    await page.goto(`${BASE}/song/${songId}?m=1`, {
      waitUntil: "domcontentloaded",
    });
    const pdfBtnWASM = page.locator(`a[href="${pdfUrl}"][target="_blank"]`);
    assert((await pdfBtnWASM.count()) === 1, "PDF link button not found in WASM");

    const ytBtnWASM = page.locator('a[title="Watch on YouTube"]');
    assert((await ytBtnWASM.count()) === 0, "Found ghost YouTube button in WASM when song has no YT");
  } finally {
    if (songId) await deleteItem(page, "song", songId).catch(() => {});
    await browser.close();
  }
});

Deno.test("song media: verify full YouTube URLs extract ID and render canonical link button", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let songId = "";

  try {
    await createAndLoginUser(page, BASE);
    const youtubeUrl = "https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=10s";
    songId = await addSong(page, uniqueTitle("Full URL YouTube Song"), {
      yt: youtubeUrl,
      data: "C G Am F\nSong with full YT url",
    });

    await page.goto(`${BASE}/song/${songId}?m=1`, {
      waitUntil: "domcontentloaded",
    });

    const ytBtn = page.locator(
      `a[href="https://www.youtube.com/watch?v=dQw4w9WgXcQ"][target="_blank"]`,
    );
    assert((await ytBtn.count()) === 1, "YouTube link button not created from full URL");

    const pdfBtn = page.locator('a[title="View PDF"]');
    assert((await pdfBtn.count()) === 0, "Unexpected PDF button found");
  } finally {
    if (songId) await deleteItem(page, "song", songId).catch(() => {});
    await browser.close();
  }
});
