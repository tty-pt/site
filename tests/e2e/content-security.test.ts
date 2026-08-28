import { chromium, type Page } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

function assert(condition: boolean, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

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
  module: "song" | "poem",
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

Deno.test("poem content strips active markup rather than rendering it", async () => {
  const browser = await chromium.launch();
  const context = await browser.newContext({ javaScriptEnabled: false });
  const page = await context.newPage();
  const title = uniqueTitle("Security Poem");
  const payload = '<svg onload="globalThis.poemXss=1">poem marker</svg>';
  const upload = await Deno.makeTempFile({ suffix: ".html" });
  let poemId = "";

  try {
    await Deno.writeTextFile(upload, payload);
    await createAndLoginUser(page, BASE);
    await page.goto(`${BASE}/poem/add`);
    await page.fill('input[name="title"]', title);
    await page.locator('input[name="body_content"]').setInputFiles(upload);
    await Promise.all([
      page.waitForURL(/\/poem\/[^/?]+$/, { timeout: 5000 }),
      page.click('form[method="POST"] button[type="submit"]'),
    ]);

    poemId = new URL(page.url()).pathname.split("/").filter(Boolean).at(-1) ??
      "";
    assert(poemId !== "", `Could not extract poem ID from ${page.url()}`);

    const body = page.locator(".poem-body");
    assert(
      await body.locator("svg").count() === 0,
      "Poem payload created an SVG element",
    );
    const html = await body.innerHTML();
    assert(
      !html.includes("<svg"),
      `Poem payload rendered raw SVG markup: ${html}`,
    );
    assert(
      !(await body.textContent())?.includes("onload"),
      "Poem payload kept an event handler attribute",
    );
  } finally {
    await Deno.remove(upload).catch(() => {});
    if (poemId) await deleteItem(page, "poem", poemId).catch(() => {});
    await browser.close();
  }
});

Deno.test("song edit textarea cannot be escaped by a closing tag", async () => {
  const browser = await chromium.launch();
  const context = await browser.newContext({ javaScriptEnabled: false });
  const page = await context.newPage();
  const title = uniqueTitle("Security Textarea Song");
  const payload =
    'before</textarea><svg onload="globalThis.textareaXss=1">after';
  let songId = "";

  try {
    await createAndLoginUser(page, BASE);
    songId = await addSong(page, title, { data: payload });
    assert(songId !== "", `Could not extract song ID from ${page.url()}`);

    await page.goto(`${BASE}/song/${songId}/edit`);
    const textarea = page.locator('textarea[name="data"]');
    assert(await textarea.count() === 1, "Expected one song data textarea");
    assert(
      await textarea.inputValue() === payload,
      "Song data was altered or escaped the textarea boundary",
    );
    assert(
      await page.locator("form svg").count() === 0,
      "Textarea payload created an SVG element",
    );
  } finally {
    if (songId) await deleteItem(page, "song", songId).catch(() => {});
    await browser.close();
  }
});

Deno.test("invalid song media values do not render active embeds", async () => {
  const browser = await chromium.launch();
  const context = await browser.newContext({ javaScriptEnabled: false });
  const page = await context.newPage();
  const invalidTitle = uniqueTitle("Security Invalid Media Song");
  let songId = "";

  try {
    await createAndLoginUser(page, BASE);

    songId = await addSong(page, invalidTitle, {
      yt: "not-a-youtube-id",
      audio: "http://example.com/insecure.mp3",
    });
    assert(songId !== "", `Could not extract song ID from ${page.url()}`);

    await page.goto(`${BASE}/song/${songId}?m=1`, {
      waitUntil: "domcontentloaded",
    });
    const invalidMedia = page.locator('[data-song-media="1"]');
    assert(
      await invalidMedia.locator("iframe").count() === 0,
      "Invalid YouTube ID created an iframe",
    );
    assert(
      await invalidMedia.locator("audio").count() === 0,
      "Non-HTTPS audio URL created an audio embed",
    );
  } finally {
    if (songId) await deleteItem(page, "song", songId).catch(() => {});
    await browser.close();
  }
});

Deno.test("valid YouTube ID renders the expected link button", async () => {
  const browser = await chromium.launch();
  const context = await browser.newContext({ javaScriptEnabled: false });
  const page = await context.newPage();
  const validTitle = uniqueTitle("Security Valid Media Song");
  const youtubeId = "dQw4w9WgXcQ";
  let songId = "";

  try {
    await createAndLoginUser(page, BASE);

    songId = await addSong(page, validTitle, { yt: youtubeId });
    assert(songId !== "", `Could not extract song ID from ${page.url()}`);

    await page.goto(`${BASE}/song/${songId}?m=1`, {
      waitUntil: "domcontentloaded",
    });
    const ytBtn = page.locator(
      `[data-song-media="1"] a[href="https://www.youtube.com/watch?v=${youtubeId}"][target="_blank"]`,
    );
    assert(
      await ytBtn.count() === 1,
      "Valid 11-character YouTube ID did not create the expected link button",
    );
    assert(
      await page.locator('[data-song-media="1"] iframe').count() === 0,
      "Found unexpected iframe in media slot",
    );
  } finally {
    if (songId) await deleteItem(page, "song", songId).catch(() => {});
    await browser.close();
  }
});
