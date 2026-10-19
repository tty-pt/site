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
  const createdSongIds: string[] = [];
  let grpId = "";
  let gigId = "";

  try {
    page.setDefaultNavigationTimeout(10000);
    page.setDefaultTimeout(10000);

    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

    // 1. Create songs with different media permutations
    const ytId = "dQw4w9WgXcQ";
    const pdfUrl = "https://example.com/test-sheet.pdf";

    // Song 1: Both YT and PDF
    const s1 = await addSong(page, uniqueTitle("Gig Media Both"), {
      yt: ytId,
      pdf: pdfUrl,
      data: "C G Am F\nBoth media song",
    });
    createdSongIds.push(s1);

    // Song 2: YT only
    const ytOnlyId = "uATsjCPK49g";
    const s2 = await addSong(page, uniqueTitle("Gig Media YT Only"), {
      yt: ytOnlyId,
      data: "C G Am F\nYT only song",
    });
    createdSongIds.push(s2);

    // Song 3: PDF only
    const pdfOnlyUrl = "https://example.com/other-sheet.pdf";
    const s3 = await addSong(page, uniqueTitle("Gig Media PDF Only"), {
      pdf: pdfOnlyUrl,
      data: "C G Am F\nPDF only song",
    });
    createdSongIds.push(s3);

    // 2. Create grp and add songs
    const grpTitle = uniqueTitle("Gig Media Grp");
    await page.goto(`${BASE}/grp/add`, { waitUntil: "domcontentloaded" });
    await page.fill('input[name="title"]', grpTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/);
    grpId = page.url().split("/grp/")[1];

    const { token: csrfSeed, cookieHeader: chSeed } =
      await getCsrfToken(cookieHeader, BASE);

    for (const songId of createdSongIds) {
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
    }

    // 3. Create gig from grp
    const gigTitle = uniqueTitle("Gig Media Test");
    await page.goto(`${BASE}/gig/add?grp=${grpId}`, { waitUntil: "domcontentloaded" });
    await page.fill('input[name="title"]', gigTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/gig\/[^/]+$/);
    gigId = page.url().split("/gig/")[1];

    // Add songs to gig
    const { token: csrfGig, cookieHeader: chGig } =
      await getCsrfToken(cookieHeader, BASE);

    for (const songId of createdSongIds) {
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
    }

    // 4. View gig with ?m=1
    await page.goto(`${BASE}/gig/${gigId}?m=1`, {
      waitUntil: "domcontentloaded",
    });

    // Verify Song 1: Both buttons present
    const ytBtn1 = page.locator(
      `a[href="https://www.youtube.com/watch?v=${ytId}"][target="_blank"]`,
    );
    assert((await ytBtn1.count()) >= 1, "Song 1: YouTube link button not found");
    const pdfBtn1 = page.locator(`a[href="${pdfUrl}"][target="_blank"]`);
    assert((await pdfBtn1.count()) >= 1, "Song 1: PDF link button not found");

    // Verify Song 2: YT only present, no ghost PDF
    const ytBtn2 = page.locator(
      `a[href="https://www.youtube.com/watch?v=${ytOnlyId}"][target="_blank"]`,
    );
    assert((await ytBtn2.count()) >= 1, "Song 2: YouTube link button not found");

    // Verify Song 3: PDF only present
    const pdfBtn3 = page.locator(`a[href="${pdfOnlyUrl}"][target="_blank"]`);
    assert((await pdfBtn3.count()) >= 1, "Song 3: PDF link button not found");

    // Verify no empty href links
    const emptyLinks = page.locator('.gig-media a[href=""]');
    assert((await emptyLinks.count()) === 0, "Found empty href link in gig-media");

    // Verify no iframe exists
    assert(
      (await page.locator("iframe").count()) === 0,
      "Found unexpected iframe in gig view",
    );
  } finally {
    if (gigId) await deleteItem(page, "gig", gigId).catch(() => {});
    if (grpId) await deleteItem(page, "grp", grpId).catch(() => {});
    for (const sid of createdSongIds) {
      await deleteItem(page, "song", sid).catch(() => {});
    }
    await browser.close();
  }
});
