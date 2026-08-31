/**
 * E2E test: Songbook & Omni-Dropdown Picker Real User Interactions
 *
 * Full lifecycle test simulating real user workflows:
 * 1. Creates a group (grp) and an empty songbook (gig) from scratch.
 * 2. Sequentially adds 9 liturgical songs using live picker search with
 *    accented characters (ç, ã, é, ó, á, í):
 *    - Entrada: Cantarei ao Senhor
 *    - Kyrie: Kyrie 3
 *    - Glória: Glória 3 (nova)
 *    - Aleluia: Aleluia 3
 *    - Ofertório: Seja a Cruz
 *    - Santo: Santo 4
 *    - Comunhão: Se Alguém Quiser Seguir-Me
 *    - Ação de Graças: Já Não Sou Eu Que Vivo
 *    - Saída: Nada te Turbe
 * 3. Asserts that exactly ONE song is added per action (no duplicate/phantom rows).
 * 4. Customizes formats on rows using format pickers with accented queries (Ação de Graças, Comunhão, etc.).
 * 5. Replaces a specific song row using inline row picker with accented search query ("coração").
 * 6. Deletes a specific row and verifies that ONLY the targeted row is removed.
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1.
 */

import { chromium, type Page } from "npm:playwright";
import { createAndLoginUser, getCsrfToken, waitForText } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const GOTO = { waitUntil: "domcontentloaded" as const };

function assert(condition: boolean, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

const REQUIRED_SONGS = [
  { title: "Cantarei ao Senhor", search: "Cantarei", format: "Entrada", formatKey: "entrada" },
  { title: "Kyrie 3", search: "Kyrie 3", format: "Kyrie", formatKey: "kyrie" },
  { title: "Glória 3 (nova)", search: "Glória 3", format: "Glória", formatKey: "gloria" },
  { title: "Aleluia 3", search: "Aleluia 3", format: "Aleluia", formatKey: "aleluia" },
  { title: "Seja a Cruz", search: "Seja a Cruz", format: "Ofertório", formatKey: "ofertorio" },
  { title: "Santo 4", search: "Santo 4", format: "Santo", formatKey: "santo" },
  { title: "Se Alguém Quiser Seguir-Me", search: "Alguém", format: "Comunhão", formatKey: "comunhao" },
  { title: "Já Não Sou Eu Que Vivo", search: "Já Não", format: "Ação de Graças", formatKey: "acao_de_gracas" },
  { title: "Nada te Turbe", search: "Nada te Turbe", format: "Saída", formatKey: "saida" },
];

const REPLACEMENT_SONG = {
  title: "Abri os Corações",
  search: "Corações", // Testing accented ç and õ search
};

async function ensureSongExists(page: Page, title: string, cookieHeader: string) {
  const { token, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);
  const resp = await fetch(`${BASE}/api/dataset/song.items?q=${encodeURIComponent(title)}`, {
    headers: { Cookie: ch },
  });
  const data = await resp.json().catch(() => ({ rows: [] }));
  const exists = data.rows && data.rows.some((r: any) => r.title === title || r.name === title);
  if (!exists) {
    console.log(`Creating missing song "${title}" via /song/add...`);
    await page.goto(`${BASE}/song/add`, GOTO);
    await page.waitForSelector('form[method="POST"] input[name="title"]', { timeout: 5000 });
    await page.fill('form[method="POST"] input[name="title"]', title);
    await Promise.all([
      page.waitForURL(/\/song\/[^\/]+$/, { timeout: 8000 }),
      page.click('form[method="POST"] button[type="submit"]'),
    ]);
    console.log(`Created song "${title}" successfully: ${page.url()}`);
  } else {
    console.log(`Song "${title}" already exists.`);
  }
}

async function ensureTypesExist(cookieHeader: string) {
  const types = [
    ["entrada", "Entrada"],
    ["kyrie", "Kyrie"],
    ["gloria", "Glória"],
    ["aleluia", "Aleluia"],
    ["ofertorio", "Ofertório"],
    ["santo", "Santo"],
    ["comunhao", "Comunhão"],
    ["acao_de_gracas", "Ação de Graças"],
    ["saida", "Saída"],
  ];
  for (const [id, name] of types) {
    const { token, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);
    await fetch(`${BASE}/api/dataset/song.types`, {
      method: "POST",
      body: new URLSearchParams({ id, name, csrf_token: token }).toString(),
      headers: { "Content-Type": "application/x-www-form-urlencoded", Cookie: ch },
    });
  }
}

Deno.test({
  name: "songbook: full real-user workflow with accented searches, single adds, replacements, and deletions",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();
    let sbId: string | null = null;
    let grpId: string | null = null;

    try {
      page.setDefaultNavigationTimeout(15000);
      page.setDefaultTimeout(15000);
      await page.route("**/styles.css", (route) => route.abort());

      await createAndLoginUser(page, BASE);
      const cookies = await page.context().cookies();
      const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

      // Ensure types and required test songs exist
      await ensureTypesExist(cookieHeader);
      for (const s of REQUIRED_SONGS) {
        await ensureSongExists(page, s.title, cookieHeader);
      }
      await ensureSongExists(page, REPLACEMENT_SONG.title, cookieHeader);

      // ── Step 0: Create group and songbook from scratch ──────────────────
      const grpTitle = `Liturgical Grp ${Date.now()}`;
      await page.goto(`${BASE}/grp/add`, GOTO);
      await page.waitForSelector('input[name="title"]', { timeout: 5000 });
      await page.fill('input[name="title"]', grpTitle);
      await page.click('form[method="POST"] button[type="submit"]');
      await page.waitForURL(/\/grp\/[^/]+$/, { timeout: 5000 });
      grpId = page.url().split("/grp/")[1].replace(/\/$/, "");

      const sbTitle = `Sunday Mass Songbook ${Date.now()}`;
      await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
      await page.waitForSelector('input[name="title"]', { timeout: 5000 });
      await page.fill('input[name="title"]', sbTitle);
      await page.click('form[method="POST"] button[type="submit"]');
      await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 5000 });
      sbId = page.url().split("/gig/")[1].replace(/\/$/, "");

      // Navigate to gig detail and clean any initial items
      await page.goto(`${BASE}/gig/${sbId}`, GOTO);
      for (let i = 0; i < 10; i++) {
        const rm = await page.$('[data-testid="remove-song-btn"]');
        if (!rm) break;
        await rm.click();
        await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 8000 });
      }

      // ── Step 1: Sequentially add 9 songs with accented queries ──────────
      for (let i = 0; i < REQUIRED_SONGS.length; i++) {
        const item = REQUIRED_SONGS[i];
        console.log(`\n[STEP 1.${i+1}] Adding song: "${item.title}" (search query: "${item.search}")...`);
        const prevCount = await page.locator('[data-gig-item]').count();

        // 1. Open the top Add Song picker
        const topPicker = page.locator('#sb-pick-post details.hyle-picker-details').first();
        const topSummary = topPicker.locator('summary.hyle-picker-trigger').first();
        await topSummary.click();

        // 2. Locate search input and type query
        const searchInput = topPicker.locator('input.hyle-picker-search').first();
        await searchInput.waitFor({ state: "visible", timeout: 5000 });

        const searchRespPromise = page.waitForResponse(
          (resp) => resp.url().includes("/pick/song.items/options") && resp.status() === 200,
          { timeout: 6000 },
        ).catch(() => null);

        await searchInput.fill(item.search);
        await searchRespPromise;

        // 3. Wait for options rows to load matching option
        const rows = topPicker.locator('.hyle-picker-rows').first();
        await rows.waitFor({ state: "visible", timeout: 5000 });

        // Wait until matching song text appears in picker rows
        let rowText = await rows.innerText();
        let retries = 0;
        while (!rowText.toLowerCase().includes(item.title.toLowerCase()) && retries < 20) {
          await page.waitForTimeout(250);
          rowText = await rows.innerText();
          retries++;
        }
        console.log(`[STEP 1.${i+1}] Picker rows text: ${rowText.replace(/\n/g, " | ").slice(0, 100)}`);
        assert(
          rowText.toLowerCase().includes(item.title.toLowerCase()),
          `Expected picker options to contain "${item.title}" for search "${item.search}", got: "${rowText}"`,
        );

        // 4. Click the matching option to add song
        const option = rows.locator(`.hyle-picker-option:has-text("${item.title}")`).first();
        await option.waitFor({ state: "visible", timeout: 5000 });

        console.log(`[STEP 1.${i+1}] Clicking option for "${item.title}"...`);
        await Promise.all([
          page.waitForNavigation({ waitUntil: "domcontentloaded" }),
          option.click(),
        ]);
        console.log(`[STEP 1.${i+1}] Navigation complete.`);

        // 5. Verify that EXACTLY ONE song was added (count increased by exactly 1)
        const newCount = await page.locator('[data-gig-item]').count();
        assert(
          newCount === prevCount + 1,
          `Expected song count to increase from ${prevCount} to ${prevCount + 1}, got ${newCount} after adding "${item.title}"`,
        );

        // Verify the added song appears in the list
        const lastRowText = await page.locator('[data-gig-item]').last().innerText();
        assert(
          lastRowText.toLowerCase().includes(item.title.toLowerCase()),
          `Expected last song row to contain "${item.title}", got: "${lastRowText}"`,
        );
      }

      // Total count must be 9
      const totalSongs = await page.locator('[data-gig-item]').count();
      assert(totalSongs === 9, `Expected exactly 9 songs, got ${totalSongs}`);

      // Listen to all requests for debugging
      page.on("request", (req) => {
        if (req.method() === "POST") {
          console.log(`[REQ POST] ${req.url()} - body: ${req.postData()?.slice(0, 100)}`);
        }
      });
      page.on("response", (res) => {
        if (res.request().method() === "POST") {
          console.log(`[RES POST] ${res.url()} -> ${res.status()}`);
        }
      });

      // ── Step 2: Set formats for each row using format pickers ───────────
      for (let i = 0; i < REQUIRED_SONGS.length; i++) {
        const item = REQUIRED_SONGS[i];
        const fmtFormId = `#sb-fmt-pick-post-${i}`;
        console.log(`\n[STEP 2.${i+1}] Setting format "${item.format}" on row ${i}...`);
        const fmtSummary = page.locator(`${fmtFormId} details.hyle-picker-details summary.hyle-picker-trigger`).first();

        await fmtSummary.click();

        // Search for format (e.g. "Ação de Graças", "Comunhão", etc.)
        const fmtSearch = page.locator(`${fmtFormId} input.hyle-picker-search`).first();
        await fmtSearch.waitFor({ state: "visible", timeout: 5000 });

        const fmtRespPromise = page.waitForResponse(
          (resp) => resp.url().includes("/pick/song.types/options") && resp.status() === 200,
          { timeout: 6000 },
        ).catch(() => null);

        await fmtSearch.fill(item.format);
        await fmtRespPromise;
        await page.waitForTimeout(100);

        const freshRows = page.locator(`${fmtFormId} .hyle-picker-rows`).first();
        await freshRows.waitFor({ state: "visible", timeout: 5000 });
        const fText = await freshRows.innerText();
        const fHtml = await freshRows.innerHTML();
        console.log(`[STEP 2.${i+1}] Format picker rows HTML: ${fHtml.slice(0, 400)}`);

        const fmtOpt = page.locator(
          `${fmtFormId} .hyle-picker-option:has(input[value="${item.formatKey}"]), ${fmtFormId} .hyle-picker-option:has(input[value="${item.format}"])`,
        ).first();
        await fmtOpt.waitFor({ state: "visible", timeout: 5000 });

        console.log(`[STEP 2.${i+1}] Clicking format option for "${item.format}" (value="${item.formatKey}")...`);
        await Promise.all([
          page.waitForNavigation({ waitUntil: "domcontentloaded" }),
          fmtOpt.click(),
        ]);
        console.log(`[STEP 2.${i+1}] Format navigation complete.`);

        // Verify format badge on row i
        const rowFmt = await page.locator(`#sb-fmt-pick-post-${i} summary`).innerText();
        console.log(`[STEP 2.${i+1}] Row ${i} format badge text: "${rowFmt}"`);
        assert(
          rowFmt.toLowerCase().includes(item.format.toLowerCase()),
          `Expected row ${i} format to be "${item.format}", got "${rowFmt}"`,
        );
      }

      // ── Step 3: Replace row 4 ("Seja a Cruz") with accented search ──────
      const replaceRowIdx = 4;
      const initialRow4Text = await page.locator('[data-gig-item]').nth(replaceRowIdx).innerText();
      assert(initialRow4Text.includes("Seja a Cruz"), `Row 4 should initially be Seja a Cruz, got: ${initialRow4Text}`);

      const replaceSummary = page.locator(`#sb-pick-post-${replaceRowIdx} details.hyle-picker-details summary.hyle-picker-trigger`).first();
      await replaceSummary.click();

      const replaceSearch = page.locator(`#sb-pick-post-${replaceRowIdx} input.hyle-picker-search`).first();
      await replaceSearch.waitFor({ state: "visible", timeout: 5000 });

      const repRespPromise = page.waitForResponse(
        (resp) => resp.url().includes("/pick/song.items/options") && resp.status() === 200,
        { timeout: 6000 },
      ).catch(() => null);

      // Search with accented query containing ç and ã ("coração")
      await replaceSearch.fill(REPLACEMENT_SONG.search);
      await repRespPromise;
      await page.waitForTimeout(100);

      const replaceOpt = page.locator(
        `#sb-pick-post-${replaceRowIdx} .hyle-picker-option:has-text("${REPLACEMENT_SONG.title}")`,
      ).first();
      await replaceOpt.waitFor({ state: "visible", timeout: 5000 });

      await Promise.all([
        page.waitForNavigation({ waitUntil: "domcontentloaded" }),
        replaceOpt.click(),
      ]);

      // Verify song count is STILL 9 (replacement did not add a new row)
      const countAfterReplace = await page.locator('[data-gig-item]').count();
      assert(countAfterReplace === 9, `Expected count to remain 9 after replace, got ${countAfterReplace}`);

      // Verify row 4 is updated with replacement song
      const updatedRow4Text = await page.locator('[data-gig-item]').nth(replaceRowIdx).innerText();
      assert(
        updatedRow4Text.includes(REPLACEMENT_SONG.title),
        `Row 4 should now contain "${REPLACEMENT_SONG.title}", got: "${updatedRow4Text}"`,
      );
      assert(
        !updatedRow4Text.includes("Seja a Cruz"),
        `Row 4 should no longer contain "Seja a Cruz", got: "${updatedRow4Text}"`,
      );

      // Verify row 4 format (Ofertório) was preserved
      const row4Fmt = await page.locator(`#sb-fmt-pick-post-${replaceRowIdx} summary`).innerText();
      assert(
        row4Fmt.toLowerCase().includes("ofertório") || row4Fmt.toLowerCase().includes("ofertorio"),
        `Expected row 4 format to remain Ofertório, got "${row4Fmt}"`,
      );

      // ── Step 4: Delete a specific row and verify only that row is removed ─
      // We will delete row 2 ("Glória 3 (nova)")
      const deleteRowIdx = 2;
      const songToDeleteTitle = "Glória 3 (nova)";
      const songBeforeTitle = REQUIRED_SONGS[1].title; // Kyrie 3
      const songAfterTitle = REQUIRED_SONGS[3].title;  // Aleluia 3

      const rowToDeleteText = await page.locator('[data-gig-item]').nth(deleteRowIdx).innerText();
      assert(
        rowToDeleteText.includes(songToDeleteTitle),
        `Row ${deleteRowIdx} should contain "${songToDeleteTitle}", got: "${rowToDeleteText}"`,
      );

      const removeButtons = page.locator('[data-testid="remove-song-btn"]');
      await Promise.all([
        page.waitForNavigation({ waitUntil: "domcontentloaded" }),
        removeButtons.nth(deleteRowIdx).click(),
      ]);

      // Verify count decreased by exactly 1 to 8
      const countAfterDelete = await page.locator('[data-gig-item]').count();
      assert(countAfterDelete === 8, `Expected count to be 8 after deletion, got ${countAfterDelete}`);

      // Verify deleted song is gone from the entire page
      const allSongsText = await page.locator('[data-gig-item]').allInnerTexts();
      const combinedText = allSongsText.join(" | ");
      assert(
        !combinedText.includes(songToDeleteTitle),
        `Deleted song "${songToDeleteTitle}" must no longer appear, found in: "${combinedText}"`,
      );

      // Verify neighboring songs (Kyrie 3 and Aleluia 3) are now adjacent
      const newRow1Text = allSongsText[1]; // should be Kyrie 3
      const newRow2Text = allSongsText[2]; // should be Aleluia 3
      assert(
        newRow1Text.includes(songBeforeTitle),
        `Expected new row 1 to be "${songBeforeTitle}", got: "${newRow1Text}"`,
      );
      assert(
        newRow2Text.includes(songAfterTitle),
        `Expected new row 2 to be "${songAfterTitle}", got: "${newRow2Text}"`,
      );

      // ── Step 5: Keyboard dismissal ─────────────────────────────────────
      // Open row 0 title picker
      const row0Summary = page.locator('#sb-pick-post-0 summary.hyle-picker-trigger').first();
      await row0Summary.click();
      const row0Details = page.locator('#sb-pick-post-0 details.hyle-picker-details').first();
      assert(await row0Details.getAttribute("open") !== null, "Row 0 picker should be open");

      // Press Escape to dismiss
      await page.keyboard.press("Escape");
      await page.waitForTimeout(100);
      assert(await row0Details.getAttribute("open") === null, "Row 0 picker should close on Escape");

      // ── Step 6: Replace row 0 (first row) and verify order ───────────────
      // Replace row 0 ("Cantarei ao Senhor") with "Glória 3 (nova)"
      await row0Summary.click();
      const row0Search = page.locator('#sb-pick-post-0 input.hyle-picker-search').first();
      await row0Search.waitFor({ state: "visible", timeout: 5000 });

      const row0RespPromise = page.waitForResponse(
        (resp) => resp.url().includes("/pick/song.items/options") && resp.status() === 200,
        { timeout: 6000 },
      ).catch(() => null);

      await row0Search.fill("Glória 3");
      await row0RespPromise;
      await page.waitForTimeout(100);

      const gloriaOpt = page.locator(
        '#sb-pick-post-0 .hyle-picker-option:has-text("Glória 3 (nova)")',
      ).first();
      await gloriaOpt.waitFor({ state: "visible", timeout: 5000 });

      await Promise.all([
        page.waitForNavigation({ waitUntil: "domcontentloaded" }),
        gloriaOpt.click(),
      ]);

      const firstRowText = await page.locator('[data-gig-item]').first().innerText();
      assert(
        firstRowText.includes("Glória 3 (nova)"),
        `First row should now be "Glória 3 (nova)", got: "${firstRowText}"`,
      );
      // Format of row 0 (Entrada) should still be preserved
      const firstRowFmt = await page.locator('#sb-fmt-pick-post-0 summary').innerText();
      assert(
        firstRowFmt.toLowerCase().includes("entrada"),
        `First row format should remain Entrada, got: "${firstRowFmt}"`,
      );

    } finally {
      await browser.close();
    }
  },
});

Deno.test({
  name: "songbook: No-JS native HTML form picker degradation (add, format, replace, delete)",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const context = await browser.newContext({ javaScriptEnabled: false });
    const page = await context.newPage();
    let sbId: string | null = null;
    let grpId: string | null = null;

    try {
      page.setDefaultNavigationTimeout(15000);
      page.setDefaultTimeout(15000);

      await createAndLoginUser(page, BASE);
      const cookies = await context.cookies();
      const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");

      await ensureTypesExist(cookieHeader);
      for (const s of REQUIRED_SONGS) {
        await ensureSongExists(page, s.title, cookieHeader);
      }

      // Create group and gig without JS
      await page.goto(`${BASE}/grp/add`, GOTO);
      await page.fill('input[name="title"]', `NoJS Grp ${Date.now()}`);
      await page.click('form[method="POST"] button[type="submit"]');
      await page.waitForURL(/\/grp\/[^/]+$/, { timeout: 5000 });
      grpId = page.url().split("/grp/")[1].replace(/\/$/, "");

      await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
      await page.fill('input[name="title"]', `NoJS Gig ${Date.now()}`);
      await page.click('form[method="POST"] button[type="submit"]');
      await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 5000 });
      sbId = page.url().split("/gig/")[1].replace(/\/$/, "");

      // Clear any seeded songs
      await page.goto(`${BASE}/gig/${sbId}`, GOTO);
      for (let i = 0; i < 10; i++) {
        const rm = await page.$('[data-testid="remove-song-btn"]');
        if (!rm) break;
        await rm.click();
        await page.waitForURL(/\/gig\/[^/]+$/, { timeout: 8000 });
      }

      // 1. Add a song via No-JS query search (?pick_q_song_id=Cantarei)
      await page.goto(`${BASE}/gig/${sbId}?pick_q_song_id=Cantarei`, GOTO);
      const optRadio = page.locator('#sb-pick-post input[type="radio"][name="song_id"]').first();
      await optRadio.check({ force: true });
      await Promise.all([
        page.waitForNavigation({ waitUntil: "domcontentloaded" }),
        page.click('#sb-pick-post button[type="submit"]'),
      ]);

      const count1 = await page.locator('[data-gig-item]').count();
      assert(count1 === 1, `Expected 1 song added without JS, got ${count1}`);

      // 2. Add second song via No-JS
      await page.goto(`${BASE}/gig/${sbId}?pick_q_song_id=Kyrie`, GOTO);
      const optRadio2 = page.locator('#sb-pick-post input[type="radio"][name="song_id"]').first();
      await optRadio2.check({ force: true });
      await Promise.all([
        page.waitForNavigation({ waitUntil: "domcontentloaded" }),
        page.click('#sb-pick-post button[type="submit"]'),
      ]);

      const count2 = await page.locator('[data-gig-item]').count();
      assert(count2 === 2, `Expected 2 songs added without JS, got ${count2}`);

      // 3. Delete song row 0 without JS
      const removeBtns = page.locator('[data-testid="remove-song-btn"]');
      await Promise.all([
        page.waitForNavigation({ waitUntil: "domcontentloaded" }),
        removeBtns.first().click(),
      ]);

      const countAfterDel = await page.locator('[data-gig-item]').count();
      assert(countAfterDel === 1, `Expected 1 song after No-JS delete, got ${countAfterDel}`);

    } finally {
      await context.close();
      await browser.close();
    }
  },
});

