import { chromium, type Page } from "npm:playwright";
import { assert } from "jsr:@std/assert";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

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

Deno.test({
  name: "gig media: verify YouTube and PDF link buttons in SSR and WASM",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  let songId = "";
  let grpId = "";
  let gigId = "";

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    // 1. Create a song with YouTube and PDF
    const songTitle = uniqueTitle("Gig Media Test Song");
    const ytId = "dQw4w9WgXcQ";
    const pdfUrl = "https://example.com/test-sheet.pdf";
    songId = await addSong(page, songTitle, {
      yt: ytId,
      pdf: pdfUrl,
      data: "C G Am F\nLyrics line",
    });

    // 2. Create grp and add song
    const grpTitle = uniqueTitle("Gig Media Grp");
    await page.goto(`${BASE}/grp/add`, { waitUntil: "domcontentloaded" });
    await page.fill('input[name="title"]', grpTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/);
    grpId = page.url().split("/grp/")[1];

    const { token: csrfSeed, cookieHeader: chSeed } =
      await getCsrfToken(cookieHeader, BASE);
    const seedBody = new URLSearchParams({
      song_id: songId,
      format: "any",
      csrf_token: csrfSeed,
    });
    await fetch(`${BASE}/api/grp/${grpId}/songs`, {
      method: "POST",
      body: seedBody.toString(),
      headers: {
        "Content-Type": "application/x-www-form-urlencoded",
        Cookie: chSeed,
      },
    });

    // 3. Create gig from grp
    const gigTitle = uniqueTitle("Gig Media Test");
    await page.goto(`${BASE}/gig/add?grp=${grpId}`, { waitUntil: "domcontentloaded" });
    await page.fill('input[name="title"]', gigTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/);
    gigId = page.url().split("/gig/")[1];

    // Add song to gig
    const { token: csrfGig, cookieHeader: chGig } =
      await getCsrfToken(cookieHeader, BASE);
    const gigSongBody = new URLSearchParams({
      song_id: songId,
      format: "any",
      csrf_token: csrfGig,
    });
    await fetch(`${BASE}/api/gig/${gigId}/songs`, {
      method: "POST",
      body: gigSongBody.toString(),
      headers: {
        "Content-Type": "application/x-www-form-urlencoded",
        Cookie: chGig,
      },
    });

    // 4. View gig with ?m=1
    await page.goto(`${BASE}/gig/${gigId}?m=1`, {
      waitUntil: "domcontentloaded",
    });

    // Verify YouTube link button is present
    const ytBtn = page.locator(
      `a[href="https://www.youtube.com/watch?v=${ytId}"][target="_blank"]`,
    );
    assert(
      (await ytBtn.count()) >= 1,
      "YouTube link button not found in gig view",
    );

    // Verify PDF link button is present
    const pdfBtn = page.locator(
      `a[href="${pdfUrl}"][target="_blank"]`,
    );
    assert(
      (await pdfBtn.count()) >= 1,
      "PDF link button not found in gig view",
    );

    // Verify no iframe exists
    assert(
      (await page.locator("iframe").count()) === 0,
      "Found unexpected iframe in gig view",
    );
  } finally {
    if (gigId) await deleteItem(page, "gig", gigId).catch(() => {});
    if (grpId) await deleteItem(page, "grp", grpId).catch(() => {});
    if (songId) await deleteItem(page, "song", songId).catch(() => {});
    await browser.close();
  }
});
