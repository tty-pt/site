import { chromium, type Page } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const GOTO = { waitUntil: "domcontentloaded" as const };

function assert(condition: boolean, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

Deno.test({
  name: "pickers: inline record creation for author (single-select) and types (multi-select)",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    
    // ── Part 1: JS ON Mode ──────────────────────────────────────────
    let context = await browser.newContext();
    let page = await context.newPage();

    try {
      page.setDefaultNavigationTimeout(15000);
      page.setDefaultTimeout(15000);
      await createAndLoginUser(page, BASE);

      const uniqueSuffix = Date.now();
      const newTypeName = "GenreJazz " + uniqueSuffix;
      const newAuthorName = "ComposerTom " + uniqueSuffix;
      const newSongTitle = "RecordCreation Song " + uniqueSuffix;

      await page.goto(`${BASE}/song/add`, GOTO);

      // 1. Create a new Type via Type Multi-Select Picker
      const typePicker = page.locator('.hyle-picker[data-hyle-picker-key="type"]');
      await typePicker.locator('summary.hyle-picker-trigger').click();

      const typeSearch = typePicker.locator('input.hyle-picker-search');
      await typeSearch.fill(newTypeName);

      const typeAddBtn = typePicker.locator('button[data-hyle-picker-add]');
      await typeAddBtn.waitFor({ state: "visible", timeout: 5000 });
      const typeAddText = await typeAddBtn.innerText();
      assert(!typeAddText.includes("&ldquo;"), `Add button text must not contain &ldquo;, got: ${typeAddText}`);
      assert(!typeAddText.includes("&rdquo;"), `Add button text must not contain &rdquo;, got: ${typeAddText}`);
      assert(typeAddText.includes("“") && typeAddText.includes("”"), `Add button text must contain curly quotes, got: ${typeAddText}`);
      const nestedTypeRows = await page.locator('.hyle-picker[data-hyle-picker-key="type"] .hyle-picker-rows .hyle-picker-rows').count();
      assert(nestedTypeRows === 0, `Type picker must not have nested .hyle-picker-rows, found ${nestedTypeRows}`);
      await typeAddBtn.click();

      // Verify type summary contains newTypeName
      let typeSummary = await typePicker.locator('.hyle-picker-values').innerText();
      while (!typeSummary.includes(newTypeName)) {
        await page.waitForTimeout(100);
        typeSummary = await typePicker.locator('.hyle-picker-values').innerText();
      }

      // Close type picker
      await typePicker.locator('summary.hyle-picker-trigger').click();

      // 2. Create a new Author via Author Single-Select Picker
      const authorPicker = page.locator('.hyle-picker[data-hyle-picker-key="author"]');
      await authorPicker.locator('summary.hyle-picker-trigger').click();

      const authorSearch = authorPicker.locator('input.hyle-picker-search');
      await authorSearch.fill(newAuthorName);

      const authorAddBtn = authorPicker.locator('button[data-hyle-picker-add]');
      await authorAddBtn.waitFor({ state: "visible", timeout: 5000 });
      const authorAddText = await authorAddBtn.innerText();
      assert(!authorAddText.includes("&ldquo;"), `Add button text must not contain &ldquo;, got: ${authorAddText}`);
      assert(!authorAddText.includes("&rdquo;"), `Add button text must not contain &rdquo;, got: ${authorAddText}`);
      assert(authorAddText.includes("“") && authorAddText.includes("”"), `Add button text must contain curly quotes, got: ${authorAddText}`);
      const nestedAuthorRows = await page.locator('.hyle-picker[data-hyle-picker-key="author"] .hyle-picker-rows .hyle-picker-rows').count();
      assert(nestedAuthorRows === 0, `Author picker must not have nested .hyle-picker-rows, found ${nestedAuthorRows}`);
      await authorAddBtn.click();

      // Verify author summary contains newAuthorName
      let authorSummary = await authorPicker.locator('.hyle-picker-values').innerText();
      while (!authorSummary.includes(newAuthorName)) {
        await page.waitForTimeout(100);
        authorSummary = await authorPicker.locator('.hyle-picker-values').innerText();
      }

      // 3. Fill title and save song
      await page.locator('form[method="POST"] input[name="title"]').fill(newSongTitle);
      await Promise.all([
        page.waitForURL(/\/song\/(?!add$)[^\/]+$/, { timeout: 10000 }),
        page.locator('form[method="POST"] button[type="submit"]').click(),
      ]);

      const songUrl = page.url();
      const bodyText = await page.locator('body').innerText();
      assert(bodyText.includes(newTypeName), `song detail should display type ${newTypeName}`);
      assert(bodyText.includes(newAuthorName), `song detail should display author ${newAuthorName}`);

      // 4. Verify edit page has author radio and type checkbox selected
      await page.goto(`${songUrl}/edit`, GOTO);
      const editTypeValues = await page.locator('.hyle-picker[data-hyle-picker-key="type"] .hyle-picker-values').innerText();
      const editAuthorValues = await page.locator('.hyle-picker[data-hyle-picker-key="author"] .hyle-picker-values').innerText();

      assert(editTypeValues.includes(newTypeName), `edit form should show selected type ${newTypeName}`);
      assert(editAuthorValues.includes(newAuthorName), `edit form should show selected author ${newAuthorName}`);

    } finally {
      await context.close();
    }

    // ── Part 2: JS OFF Mode ─────────────────────────────────────────
    context = await browser.newContext({ javaScriptEnabled: false });
    page = await context.newPage();

    try {
      page.setDefaultNavigationTimeout(15000);
      page.setDefaultTimeout(15000);
      await createAndLoginUser(page, BASE);

      const nojsSuffix = Date.now();
      const nojsTypeName = "NoJSType " + nojsSuffix;
      const nojsAuthorName = "NoJSAuthor " + nojsSuffix;
      const nojsTitle = "NoJS Song " + nojsSuffix;

      // Seed entities via API endpoint
      const csrfResp = await page.request.get(`${BASE}/api/csrf`);
      const csrf = await csrfResp.text();

      await page.request.post(`${BASE}/api/dataset/song.types`, {
        form: { name: nojsTypeName, csrf_token: csrf },
      });
      await page.request.post(`${BASE}/api/dataset/song.authors`, {
        form: { name: nojsAuthorName, csrf_token: csrf },
      });

      await page.goto(`${BASE}/song/add`, GOTO);
      await page.locator('form[method="POST"] input[name="title"]').fill(nojsTitle);

      // Search for type in sibling GET form
      await page.locator('.hyle-picker[data-hyle-picker-key="type"] details summary').click();
      await page.locator('input[name="pick_q_type"]').fill(nojsTypeName);
      await Promise.all([
        page.waitForNavigation(),
        page.locator('input[name="pick_q_type"]').press("Enter"),
      ]);

      // Refill title after no-js reload
      await page.locator('form[method="POST"] input[name="title"]').fill(nojsTitle);

      // Open type details if closed, and check the type checkbox
      const typeDetails = page.locator('.hyle-picker[data-hyle-picker-key="type"] details');
      if (await typeDetails.getAttribute("open") === null) {
        await typeDetails.locator('summary').click();
      }
      const typeOption = page.locator(`.hyle-picker[data-hyle-picker-key="type"] label.hyle-picker-option:has-text("${nojsTypeName}") input[type="checkbox"]`);
      await typeOption.check({ force: true });

      // Close type details in JS-off mode so it doesn't overlay subsequent fields
      await typeDetails.locator('summary').click();

      // Search for author in sibling GET form
      const authorDetails = page.locator('.hyle-picker[data-hyle-picker-key="author"] details');
      if (await authorDetails.getAttribute("open") === null) {
        await authorDetails.locator('summary').click();
      }
      await page.locator('input[name="pick_q_author"]').fill(nojsAuthorName);
      await Promise.all([
        page.waitForNavigation(),
        page.locator('input[name="pick_q_author"]').press("Enter"),
      ]);

      // Refill title & recheck type if needed
      await page.locator('form[method="POST"] input[name="title"]').fill(nojsTitle);
      const recheckedTypeOption = page.locator(`.hyle-picker[data-hyle-picker-key="type"] label.hyle-picker-option:has-text("${nojsTypeName}") input[type="checkbox"]`);
      if (await recheckedTypeOption.count() > 0) {
        const recheckDetails = page.locator('.hyle-picker[data-hyle-picker-key="type"] details');
        if (await recheckDetails.getAttribute("open") === null) {
          await recheckDetails.locator('summary').click();
        }
        await recheckedTypeOption.check({ force: true });
        await recheckDetails.locator('summary').click();
      }

      // Check the author radio
      const authorOption = page.locator(`.hyle-picker[data-hyle-picker-key="author"] label.hyle-picker-option:has-text("${nojsAuthorName}") input[type="radio"]`);
      if (await authorOption.count() > 0) {
        const recheckAuthDetails = page.locator('.hyle-picker[data-hyle-picker-key="author"] details');
        if (await recheckAuthDetails.getAttribute("open") === null) {
          await recheckAuthDetails.locator('summary').click();
        }
        await authorOption.check({ force: true });
      }

      // Submit form
      await Promise.all([
        page.waitForURL(/\/song\/(?!add$)[^\/]+$/, { timeout: 10000 }),
        page.locator('form[method="POST"] button[type="submit"]').click(),
      ]);

      const nojsBodyText = await page.locator('body').innerText();
      assert(nojsBodyText.includes(nojsTitle), `song detail should display title ${nojsTitle}`);
      assert(nojsBodyText.includes(nojsAuthorName), `song detail should display author ${nojsAuthorName}`);

    } finally {
      await context.close();
      await browser.close();
    }
  },
});

Deno.test({
  name: "pickers: keyboard accessibility - add via Enter in search and Tab+Space/Enter on add button",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const context = await browser.newContext();
    const page = await context.newPage();

    try {
      page.setDefaultNavigationTimeout(15000);
      page.setDefaultTimeout(15000);
      await createAndLoginUser(page, BASE);

      const uniqueSuffix = Date.now();
      const enterTypeName = "KeyEnterType " + uniqueSuffix;
      const tabAuthorName = "KeyTabAuthor " + uniqueSuffix;
      const songTitle = "Keyboard A11y Song " + uniqueSuffix;

      await page.goto(`${BASE}/song/add`, GOTO);

      // ── 1. Type picker: Add by pressing Enter directly in the search box ──
      const typePicker = page.locator('.hyle-picker[data-hyle-picker-key="type"]');
      await typePicker.locator('summary.hyle-picker-trigger').click();

      const typeSearch = typePicker.locator('input.hyle-picker-search');
      await typeSearch.focus();
      await typeSearch.fill(enterTypeName);

      const typeAddBtn = typePicker.locator('button[data-hyle-picker-add]');
      await typeAddBtn.waitFor({ state: "visible", timeout: 5000 });

      // Press Enter in the search box to trigger creation
      await typeSearch.press("Enter");

      // Verify type summary contains the newly created type
      let typeSummary = await typePicker.locator('.hyle-picker-values').innerText();
      while (!typeSummary.includes(enterTypeName)) {
        await page.waitForTimeout(100);
        typeSummary = await typePicker.locator('.hyle-picker-values').innerText();
      }

      // Close type picker
      await typePicker.locator('summary.hyle-picker-trigger').click();

      // ── 2. Author picker: Add by Tabbing to the Add button and pressing Space/Enter ──
      const authorPicker = page.locator('.hyle-picker[data-hyle-picker-key="author"]');
      await authorPicker.locator('summary.hyle-picker-trigger').click();

      const authorSearch = authorPicker.locator('input.hyle-picker-search');
      await authorSearch.focus();
      await authorSearch.fill(tabAuthorName);

      const authorAddBtn = authorPicker.locator('button[data-hyle-picker-add]');
      await authorAddBtn.waitFor({ state: "visible", timeout: 5000 });

      // Tab from search box to the Add button
      await page.keyboard.press("Tab");

      // Press Space on the focused Add button to activate it
      await page.keyboard.press("Space");

      // Verify author summary contains the newly created author
      let authorSummary = await authorPicker.locator('.hyle-picker-values').innerText();
      while (!authorSummary.includes(tabAuthorName)) {
        await page.waitForTimeout(100);
        authorSummary = await authorPicker.locator('.hyle-picker-values').innerText();
      }

      // ── 3. Fill title and save song ──
      await page.locator('form[method="POST"] input[name="title"]').fill(songTitle);
      await Promise.all([
        page.waitForURL(/\/song\/(?!add$)[^\/]+$/, { timeout: 10000 }),
        page.locator('form[method="POST"] button[type="submit"]').click(),
      ]);

      const bodyText = await page.locator('body').innerText();
      assert(bodyText.includes(enterTypeName), `song detail should display type ${enterTypeName}`);
      assert(bodyText.includes(tabAuthorName), `song detail should display author ${tabAuthorName}`);

    } finally {
      await context.close();
      await browser.close();
    }
  },
});

Deno.test({
  name: "pickers: non-creatable entity datasets like grp in /gig/add have no inline add button",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const context = await browser.newContext();
    const page = await context.newPage();

    try {
      page.setDefaultNavigationTimeout(15000);
      page.setDefaultTimeout(15000);
      await createAndLoginUser(page, BASE);

      // 1. Visit /gig/add and open group picker
      await page.goto(`${BASE}/gig/add`, GOTO);

      const grpPicker = page.locator('.hyle-picker[data-hyle-picker-key="grp"]');
      assert(await grpPicker.count() > 0, "Group picker must be present on /gig/add");
      assert(await grpPicker.getAttribute("data-hyle-picker-addable") === null, "Group picker must NOT have data-hyle-picker-addable attribute");

      await grpPicker.locator('summary.hyle-picker-trigger').click();
      const grpSearch = grpPicker.locator('input.hyle-picker-search');
      await grpSearch.fill("Unregistered Random Band");

      // Wait a moment for any DOM debouncing / slot fetch
      await page.waitForTimeout(300);

      // Verify no add button is rendered
      const addBtn = grpPicker.locator('button[data-hyle-picker-add], .hyle-picker-add');
      const addCount = await addBtn.count();
      assert(addCount === 0, `Group picker on /gig/add must NOT render an add button, found ${addCount}`);

      // 2. Verify /grp listing displays valid titles and no empty <a></a> links
      await page.goto(`${BASE}/grp`, GOTO);
      const rows = page.locator('tbody tr');
      const rowCount = await rows.count();
      assert(rowCount > 0, "/grp listing must have visible group rows");
      for (let i = 0; i < rowCount; i++) {
        const titleLink = rows.nth(i).locator('td[data-label="Title"] a').first();
        const text = (await titleLink.innerText()).trim();
        assert(text.length > 0, `Row ${i} on /grp listing must have non-empty title, got empty string`);
      }

    } finally {
      await context.close();
      await browser.close();
      try {
        for await (const entry of Deno.readDir("var/song.types")) {
          if (entry.isDirectory && (entry.name.startsWith("genrejazz") || entry.name.startsWith("nojstype") || entry.name.startsWith("keyentertype"))) {
            await Deno.remove(`var/song.types/${entry.name}`, { recursive: true }).catch(() => {});
          }
        }
        for await (const entry of Deno.readDir("var/song.authors")) {
          if (entry.isDirectory && (entry.name.startsWith("composertom") || entry.name.startsWith("nojsauthor") || entry.name.startsWith("keytabauthor"))) {
            await Deno.remove(`var/song.authors/${entry.name}`, { recursive: true }).catch(() => {});
          }
        }
      } catch {}
    }
  },
});

