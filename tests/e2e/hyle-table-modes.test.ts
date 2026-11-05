/**
 * E2E test: Hyle tables features (sorting, pagination, filtering)
 *
 * Verifies that:
 * 1. With client-side scripts disabled (No-JS / SSR):
 *    - Sorting by column asc and desc works via page reload and links.
 *    - Paging works via GET form submission with page reload, preserving sort.
 *    - Filtering works via GET form submission with page reload, preserving sort.
 * 2. Without reloading the page (client-side scripts / WASM enabled):
 *    - Sorting by column asc and desc works in-place with zero page reload.
 *    - Paging works in-place with zero page reload.
 *    - Filtering works in-place with zero page reload.
 *    - Caching in hyle-source eliminates redundant network roundtrips on revisited table views.
 */

import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";

Deno.test("hyle tables: works with client-side scripts disabled (No-JS / page reloads)", async () => {
  const browser = await chromium.launch();
  const context = await browser.newContext({ javaScriptEnabled: false });
  const page = await context.newPage();

  try {
    // 1. Initial page load
    await page.goto(`${BASE}/song/?custom=1`, { waitUntil: "load" });
    await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

    const rowsInit = page.locator("tr.hyle-row-clickable");
    const countInit = await rowsInit.count();
    if (countInit !== 10) {
      throw new Error(`Expected 10 rows on initial load, got ${countInit}`);
    }

    // 2. Sort by Title ascending (click header link)
    const titleSortBtn = page.locator('th a.hyle-sort-button[data-field="title"]');
    await Promise.all([
      page.waitForNavigation({ waitUntil: "load" }),
      titleSortBtn.click(),
    ]);

    if (!page.url().includes("sort=title:asc")) {
      throw new Error(`Expected URL to contain sort=title:asc, got ${page.url()}`);
    }
    const ascTitle = (await page.locator("tr.hyle-row-clickable td:first-child a").first().textContent())?.trim() ?? "";
    if (!ascTitle) {
      throw new Error("Could not read first row title for ascending sort");
    }

    // 3. Sort by Title descending (click header link again)
    const titleSortBtnDesc = page.locator('th a.hyle-sort-button[data-field="title"]');
    await Promise.all([
      page.waitForNavigation({ waitUntil: "load" }),
      titleSortBtnDesc.click(),
    ]);

    if (!page.url().includes("sort=title:desc")) {
      throw new Error(`Expected URL to contain sort=title:desc, got ${page.url()}`);
    }
    const descTitle = (await page.locator("tr.hyle-row-clickable td:first-child a").first().textContent())?.trim() ?? "";
    if (!descTitle) {
      throw new Error("Could not read first row title for descending sort");
    }

    if (ascTitle === descTitle) {
      throw new Error(`Ascending title (${ascTitle}) should differ from descending title (${descTitle})`);
    }

    // 4. Pagination with sort preservation: click Next
    const nextBtn = page.locator('button[name="page"]:has-text("Next")');
    await Promise.all([
      page.waitForNavigation({ waitUntil: "load" }),
      nextBtn.click(),
    ]);

    if (!page.url().includes("page=2")) {
      throw new Error(`Expected URL to contain page=2, got ${page.url()}`);
    }
    if (!decodeURIComponent(page.url()).includes("sort=title:desc")) {
      throw new Error(`Expected URL to preserve sort=title:desc across pagination, got ${page.url()}`);
    }
    const bodyP2 = await page.textContent("body") ?? "";
    if (!bodyP2.includes("Page 2")) {
      throw new Error('Expected "Page 2" text indicator on page 2');
    }

    // 5. Filtering with sort preservation: check type=comunhao and click Apply
    await page.locator('details.hyle-multiselect summary').click();
    await page.locator('details.hyle-multiselect input[name="type"][value="comunhao"]').check();
    const applyBtn = page.locator('.hyle-filter-actions button[type="submit"]');
    await Promise.all([
      page.waitForNavigation({ waitUntil: "load" }),
      applyBtn.click(),
    ]);

    if (!page.url().includes("type=comunhao")) {
      throw new Error(`Expected URL to contain type=comunhao, got ${page.url()}`);
    }
    if (!decodeURIComponent(page.url()).includes("sort=title:desc")) {
      throw new Error(`Expected URL to preserve sort=title:desc across filter submit, got ${page.url()}`);
    }

    const filteredRows = page.locator("tr.hyle-row-clickable");
    const filteredCount = await filteredRows.count();
    if (filteredCount <= 0) {
      throw new Error("Expected at least one filtered row");
    }

    const typeCells = await filteredRows.locator("td:nth-child(2)").allTextContents();
    for (const t of typeCells) {
      if (!t.includes("Comunhão")) {
        throw new Error(`Expected filtered row to have Comunhão type, got: ${t}`);
      }
    }

    // 6. Single-select author radio picker persistence across page reload (No-JS)
    const authorDetails = page.locator('details.hyle-singleselect[data-hyle-ss="author"]');
    await authorDetails.locator("summary").click();
    const authorRadio = page.locator('details.hyle-singleselect input[name="author"][value="j_p_martins"]');
    await authorRadio.check();
    await Promise.all([
      page.waitForNavigation({ waitUntil: "load" }),
      page.locator('.hyle-filter-actions button[type="submit"]').click(),
    ]);

    await authorDetails.locator("summary").click();
    if (!await authorRadio.isChecked()) {
      throw new Error("Expected author radio j_p_martins to remain checked after reload in No-JS mode");
    }
    const checkedAuthorRadios = await page.locator('details.hyle-singleselect input[name="author"]:checked').count();
    if (checkedAuthorRadios !== 1) {
      throw new Error(`Expected exactly 1 checked author radio, got ${checkedAuthorRadios}`);
    }

    // 7. Clear author filter via "All Authors" option in No-JS mode
    const allAuthorRadio = page.locator('details.hyle-singleselect .hyle-ss-option[data-label="All Authors"] input[type="radio"]');
    await allAuthorRadio.check();
    await Promise.all([
      page.waitForNavigation({ waitUntil: "load" }),
      page.locator('.hyle-filter-actions button[type="submit"]').click(),
    ]);
    await authorDetails.locator("summary").click();
    if (!await allAuthorRadio.isChecked()) {
      throw new Error("Expected All Authors radio to be checked after clearing filter in No-JS mode");
    }
    if (await authorRadio.isChecked()) {
      throw new Error("Expected author radio j_p_martins to be unchecked after clearing filter in No-JS mode");
    }
  } finally {
    await browser.close();
  }
});

Deno.test("hyle tables: works without reloading the page when client-side scripts are enabled (WASM)", async () => {
  const browser = await chromium.launch();
  const context = await browser.newContext({ javaScriptEnabled: true });
  const page = await context.newPage();

  try {
    let loads = 0;
    page.on("load", () => {
      loads++;
    });

    const requestedUrls: string[] = [];
    page.on("request", (req) => {
      requestedUrls.push(req.url());
    });

    // 1. Initial page load
    await page.goto(`${BASE}/song/?custom=1`, { waitUntil: "load" });
    await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
    // Allow WASM hydration to attach
    await page.waitForTimeout(2000);

    const initialLoads = loads;

    // 2. Interactive Sort by Title ascending without reload
    const titleSortBtn = page.locator('th a.hyle-sort-button[data-field="title"]');
    await titleSortBtn.click();
    await page.waitForTimeout(1000);

    if (loads !== initialLoads) {
      throw new Error(`Page reloaded on sort click: loads was ${initialLoads}, now ${loads}`);
    }
    if (!page.url().includes("sort=title:asc")) {
      throw new Error(`Expected URL to be updated to sort=title:asc via pushState, got ${page.url()}`);
    }

    const wasmAscTitle = (await page.locator("tr.hyle-row-clickable td:first-child a").first().textContent())?.trim() ?? "";
    if (!wasmAscTitle) {
      throw new Error("Could not read first row title for WASM ascending sort");
    }

    // 3. Interactive Sort by Title descending without reload
    await titleSortBtn.click();
    await page.waitForTimeout(1000);

    if (loads !== initialLoads) {
      throw new Error(`Page reloaded on sort desc click: loads was ${initialLoads}, now ${loads}`);
    }
    if (!page.url().includes("sort=title:desc")) {
      throw new Error(`Expected URL to be updated to sort=title:desc, got ${page.url()}`);
    }

    const wasmDescTitle = (await page.locator("tr.hyle-row-clickable td:first-child a").first().textContent())?.trim() ?? "";
    if (!wasmDescTitle) {
      throw new Error("Could not read first row title for WASM descending sort");
    }

    if (wasmAscTitle === wasmDescTitle) {
      throw new Error(`WASM asc title (${wasmAscTitle}) should differ from desc title (${wasmDescTitle})`);
    }

    // 4. Interactive Paging without reload
    const nextBtn = page.locator('button[data-action="2"]:has-text("Next")');
    await nextBtn.click();
    await page.waitForTimeout(1000);

    if (loads !== initialLoads) {
      throw new Error(`Page reloaded on paging click: loads was ${initialLoads}, now ${loads}`);
    }
    if (!page.url().includes("page=2")) {
      throw new Error(`Expected URL to contain page=2 via pushState, got ${page.url()}`);
    }

    const bodyTextP2 = await page.textContent("body") ?? "";
    if (!bodyTextP2.includes("Page 2")) {
      throw new Error('Expected "Page 2" text indicator on page 2 in WASM mode');
    }

    // 5. Caching in hyle-source: navigating back to page 1 should resolve from client cache
    const initialFetchCount = requestedUrls.filter(u => u.includes("format=json")).length;
    const prevBtn = page.locator('button[data-action="2"]:has-text("Prev")');
    await prevBtn.click();
    await page.waitForTimeout(1000);

    if (loads !== initialLoads) {
      throw new Error(`Page reloaded on prev paging click: loads was ${initialLoads}, now ${loads}`);
    }

    const bodyTextP1 = await page.textContent("body") ?? "";
    if (!bodyTextP1.includes("Page 1")) {
      throw new Error('Expected "Page 1" text indicator on return to page 1 in WASM mode');
    }

    const newFetchCount = requestedUrls.filter(u => u.includes("format=json")).length;
    // With client-side caching in hyle-source, page 1 was already cached so no additional fetch was issued
    if (newFetchCount !== initialFetchCount) {
      throw new Error(
        `Expected client-side cache hit with 0 network fetches (initial: ${initialFetchCount}, now: ${newFetchCount})`,
      );
    }

    // 6. Interactive Filtering without reload
    await page.locator('details.hyle-multiselect summary').click();
    await page.locator('details.hyle-multiselect input[name="type"][value="comunhao"]').check();
    const applyBtn = page.locator('.hyle-filter-actions button[type="submit"]');
    await applyBtn.click();
    await page.waitForTimeout(1000);

    if (loads !== initialLoads) {
      throw new Error(`Page reloaded on filter submit: loads was ${initialLoads}, now ${loads}`);
    }
    if (!page.url().includes("type=comunhao")) {
      throw new Error(`Expected URL to contain type=comunhao, got ${page.url()}`);
    }

    // 7. Single-select author radio picker persistence across WASM in-place update
    const authorDetails = page.locator('details.hyle-singleselect[data-hyle-ss="author"]');
    await authorDetails.locator("summary").click();
    const authorRadio = page.locator('details.hyle-singleselect input[name="author"][value="j_p_martins"]');
    await authorRadio.check();
    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForTimeout(1000);

    if (loads !== initialLoads) {
      throw new Error(`Page reloaded on author filter submit: loads was ${initialLoads}, now ${loads}`);
    }
    if (!page.url().includes("author=j_p_martins")) {
      throw new Error(`Expected URL to contain author=j_p_martins, got ${page.url()}`);
    }

    // Bring up dropdown again and verify radio remains checked in DOM
    await authorDetails.locator("summary").click();
    if (!await authorRadio.isChecked()) {
      throw new Error("Expected author radio j_p_martins to remain checked after bringing up dropdown in WASM mode");
    }
    const checkedAuthorRadios = await page.locator('details.hyle-singleselect input[name="author"]:checked').count();
    if (checkedAuthorRadios !== 1) {
      throw new Error(`Expected exactly 1 checked author radio, got ${checkedAuthorRadios}`);
    }

    // 8. Click-to-deselect active author radio and verify filter clear without reload
    await authorRadio.click();
    await page.waitForTimeout(50);
    const allAuthorRadio = page.locator('details.hyle-singleselect .hyle-ss-option[data-label="All Authors"] input[type="radio"]');
    if (!await allAuthorRadio.isChecked()) {
      throw new Error("Expected All Authors radio to be checked after click-to-deselect");
    }
    if (await authorRadio.isChecked()) {
      throw new Error("Expected author radio to be unchecked after click-to-deselect");
    }

    await page.locator('.hyle-filter-actions button[type="submit"]').click();
    await page.waitForTimeout(1000);
    if (loads !== initialLoads) {
      throw new Error(`Page reloaded on clearing author filter: loads was ${initialLoads}, now ${loads}`);
    }
    if (page.url().includes("author=j_p_martins")) {
      throw new Error(`Expected URL to not contain author=j_p_martins after clearing, got ${page.url()}`);
    }

    await authorDetails.locator("summary").click();
    if (!await allAuthorRadio.isChecked()) {
      throw new Error("Expected All Authors radio to remain checked after filter clear in WASM mode");
    }
  } finally {
    await browser.close();
  }
});
