/**
 * Diagnostic tests: verify Dioxus client mounting and hydration.
 *
 * These tests check whether the Dioxus WASM client correctly mounts
 * hydrate from the SSR-rendered body tree.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SONG_ID = "a_alegria_esta_no_coracao";

Deno.test("dioxus mount: verify #main element exists in SSR", async () => {
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

Deno.test("dioxus mount: verify hydration payload is present", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  const pageErrors: string[] = [];
  page.on("pageerror", (err) => {
    pageErrors.push(err.message);
  });

  try {
    await createAndLoginUser(page, BASE);
    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.waitForSelector("#main", { timeout: 5000, state: "attached" });
    await page.waitForTimeout(2000);

    const hydrationData = await page.evaluate(() => (globalThis as any).initial_dioxus_hydration_data);
    if (typeof hydrationData !== "string" || hydrationData.length === 0) {
      throw new Error("initial_dioxus_hydration_data missing or empty");
    }

    const bootstrapError = pageErrors.find((msg) => msg.includes("atob") || msg.includes("initial_dioxus_hydration_data"));
    if (bootstrapError) {
      throw new Error(`hydration bootstrap failed: ${bootstrapError}`);
    }
  } finally {
    await browser.close();
  }
});

Deno.test("dioxus mount: verify WASM module loads (song-client.js)", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);

    const wasmRequests: string[] = [];
    page.on("request", (req) => {
      if (req.url().includes("song-client")) {
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

Deno.test("dioxus mount: verify data-modules attribute on body", async () => {
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

Deno.test("dioxus mount: verify select element exists after handoff", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);
    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.waitForSelector("#main", { timeout: 5000, state: "attached" });
    await page.waitForTimeout(2000);

    const selectCount = await page.$$eval('select[name="t"]', (els) => els.length);
    console.log(`Found ${selectCount} select[name="t"] elements`);

    if (selectCount === 0) {
      throw new Error("No select elements found after Dioxus handoff");
    }
    if (selectCount > 1) {
      console.log(`WARNING: Found ${selectCount} select[name="t"] elements - possible duplicate SSR/Dioxus content`);
    }
  } finally {
    await browser.close();
  }
});

Deno.test("dioxus mount: verify Dioxus renders into #main", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);
    
    const allConsoleMessages: string[] = [];
    page.on("console", (msg) => {
      allConsoleMessages.push(`[${msg.type()}] ${msg.text()}`);
    });

    const pageErrors: string[] = [];
    page.on("pageerror", (err) => {
      pageErrors.push(err.message);
    });

    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.waitForSelector("#main", { timeout: 5000, state: "attached" });
    await page.waitForTimeout(2000);

    const hydrationCount = await page.locator('#main [data-node-hydration]').count();

    if (Deno.env.get("DEBUG")) {
      console.log("All console messages:");
      for (const msg of allConsoleMessages) {
        console.log(`  ${msg}`);
      }
      console.log("Page errors:");
      for (const err of pageErrors) {
        console.log(`  ${err}`);
      }
      const mainHtml = await page.$eval("#main", (el) => el.innerHTML);
      console.log(`#main innerHTML length: ${mainHtml.length}`);
      console.log(`#main outerHTML (first 500): ${await page.$eval("#main", (el) => el.outerHTML).then(s => s.slice(0, 500))}`);
      console.log(`Elements with data-node-hydration in #main: ${hydrationCount}`);
      const childTags = await page.$$eval("#main > *", (els) => els.map((el) => el.tagName + (el.id ? "#" + el.id : "") + (el.className ? "." + el.className.split(" ").slice(0, 3).join(".") : "")));
      console.log(`Direct children of #main: ${childTags.join(", ")}`);
    }

    if (hydrationCount === 0) {
      throw new Error(`Dioxus did not render into #main. Console: ${allConsoleMessages.join("; ")}. Errors: ${pageErrors.join("; ")}`);
    }
  } finally {
    await browser.close();
  }
});

Deno.test("dioxus mount: verify transpose API call on key change", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);
    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.waitForSelector("#main", { timeout: 5000, state: "attached" });
    await page.waitForTimeout(2000);

    // Open the sidebar menu to access transpose controls
    await page.locator('#menu-functions').evaluate((el) => {
      const input = el;
      input.checked = true;
      input.dispatchEvent(new Event("change", { bubbles: true }));
    });
    await page.locator('.functions').waitFor({ state: 'visible', timeout: 5000 });

    const transposeRequests: string[] = [];
    page.on("request", (req) => {
      if (req.url().includes(`/api/song/${SONG_ID}/transpose`)) {
        transposeRequests.push(req.url());
      }
    });

    const selectEl = page.locator('#transpose-form select[name="t"]');
    await selectEl.waitFor({ state: "visible", timeout: 5000 });
    await selectEl.selectOption({ index: 5 });
    await page.waitForTimeout(2000);

    if (transposeRequests.length === 0) {
      throw new Error("No transpose API call made after key change - Dioxus handlers not attached");
    }
  } finally {
    await browser.close();
  }
});

Deno.test("dioxus mount: verify prefs API call on checkbox change", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  const prefsRequests: string[] = [];

  try {
    await createAndLoginUser(page, BASE);
    await page.goto(`${BASE}/song/${SONG_ID}`);
    await page.waitForSelector("#main", { timeout: 5000, state: "attached" });
    await page.waitForTimeout(2000);

    await page.locator('#menu-functions').evaluate((el) => {
      const input = el;
      input.checked = true;
      input.dispatchEvent(new Event("change", { bubbles: true }));
    });
    await page.locator('.functions').waitFor({ state: 'visible', timeout: 5000 });

    const latinCheckbox = page.locator('input[name="l"]').first();
    await latinCheckbox.waitFor({ state: "visible", timeout: 5000 });

    page.on("request", (req) => {
      if (req.url().includes("/api/song/prefs") && req.method() === "POST") {
        prefsRequests.push(req.url());
      }
    });

    await Promise.all([
      page.waitForRequest(
        (req) => req.url().includes("/api/song/prefs") && req.method() === "POST",
        { timeout: 10000 },
      ),
      latinCheckbox.evaluate((el) => {
        const input = el;
        input.checked = true;
        input.dispatchEvent(new Event("input", { bubbles: true }));
      }),
    ]);

    if (prefsRequests.length === 0) {
      throw new Error("No prefs API call made after checkbox change - Dioxus handlers not attached");
    }
  } finally {
    await browser.close();
  }
});
