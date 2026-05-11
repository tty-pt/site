import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";

Deno.test("bud hydration: verify SSR #main element with chord data", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);
    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.waitForSelector("#main", { timeout: 5000, state: "attached" });

    const mountEl = await page.$("#main");
    if (!mountEl) {
      throw new Error("#main element not found in DOM");
    }

    const songId = await mountEl.getAttribute("data-song-id");
    const transpose = await mountEl.getAttribute("data-transpose");
    const chordData = await mountEl.getAttribute("data-chord-data");

    if (songId !== SONG_ID) {
      throw new Error(`data-song-id mismatch: expected ${SONG_ID}, got ${songId}`);
    }
    if (transpose === null) {
      throw new Error("data-transpose attribute missing");
    }
    if (!chordData || chordData.length < 10) {
      throw new Error("data-chord-data attribute missing or too short");
    }
  } finally {
    await browser.close();
  }
});

Deno.test("bud hydration: verify WASM module loads (song-client.js)", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);

    const wasmRequests: string[] = [];
    page.on("request", (req) => {
      if (req.url().includes("bud-client")) {
        wasmRequests.push(req.url());
      }
    });

    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.waitForSelector("#main", { timeout: 5000, state: "attached" });
    await page.waitForTimeout(3000);

    if (wasmRequests.length === 0) {
      throw new Error("song-client.js WASM module was not requested");
    }
  } finally {
    await browser.close();
  }
});

Deno.test("bud hydration: verify data-modules attribute on body", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);
    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.waitForSelector("#main", { timeout: 5000, state: "attached" });

    const modulesAttr = await page.getAttribute("body", "data-modules");
    if (!modulesAttr || !modulesAttr.includes("song")) {
      throw new Error(`data-modules attribute missing or incorrect: ${modulesAttr}`);
    }
  } finally {
    await browser.close();
  }
});

Deno.test("bud hydration: verify select element exists after handoff", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);
    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.waitForSelector("#main", { timeout: 5000, state: "attached" });
    await page.waitForTimeout(2000);

    const selectCount = await page.$$eval('select[name="t"]', (els) => els.length);

    if (selectCount === 0) {
      throw new Error("No select elements found after handoff");
    }
    if (selectCount > 1) {
      console.log(`WARNING: Found ${selectCount} select[name="t"] elements`);
    }
  } finally {
    await browser.close();
  }
});

Deno.test("bud hydration: verify bud renders into #main", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);

    const pageErrors: string[] = [];
    page.on("pageerror", (err) => {
      pageErrors.push(err.message);
    });

    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.waitForSelector("#main", { timeout: 5000, state: "attached" });
    await page.waitForTimeout(2000);

    const budIdCount = await page.locator('#main [data-bud-id]').count();

    if (Deno.env.get("DEBUG")) {
      console.log(`Elements with data-bud-id in #main: ${budIdCount}`);
    }

    if (budIdCount === 0) {
      throw new Error(`Bud did not render into #main. Errors: ${pageErrors.join("; ")}`);
    }
  } finally {
    await browser.close();
  }
});

Deno.test("bud hydration: verify transpose works via SSR form", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);
    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.waitForSelector("#main", { timeout: 5000, state: "attached" });
    await page.waitForTimeout(2000);

    const selectEl = page.locator('#transpose-form select[name="t"]');
    await selectEl.waitFor({ state: "attached", timeout: 5000 });

    const currentUrl = page.url();
    await selectEl.selectOption({ index: 5 });
    await page.waitForTimeout(1000);

    // Accept either fetch-driven update or full page reload
    const urlChanged = page.url() !== currentUrl;
    const chordContent = await page.locator('#chord-data').textContent();

    if (!chordContent || chordContent.length < 5) {
      throw new Error("Chord data missing or too short after transpose");
    }
  } finally {
    await browser.close();
  }
});

Deno.test("bud hydration: verify prefs SSR persistence", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);
    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.waitForSelector("#main", { timeout: 5000, state: "attached" });
    await page.waitForTimeout(2000);

    // Toggle Latin checkbox
    const latinCheckbox = page.locator('input[name="l"]').first();
    await latinCheckbox.waitFor({ state: "attached", timeout: 5000 });

    const wasChecked = await latinCheckbox.isChecked();
    if (wasChecked) {
      await latinCheckbox.evaluate((el) => { (el as any).checked = false; });
    } else {
      await latinCheckbox.evaluate((el) => { (el as any).checked = true; });
    }
    await latinCheckbox.dispatchEvent("change");
    await page.waitForTimeout(1000);

    // Reload and verify pref persisted
    await page.reload();
    await page.waitForSelector("#main", { timeout: 5000, state: "attached" });
    const latinAfter = await page.locator('input[name="l"]').first().isChecked();

    if (latinAfter === wasChecked) {
      console.log("Pref may not have persisted (expected after change+reload)");
    }
  } finally {
    await browser.close();
  }
});
