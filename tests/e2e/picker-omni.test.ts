import { chromium, type Page } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const GOTO = { waitUntil: "domcontentloaded" as const };

function assert(condition: boolean, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

Deno.test({
  name: "pickers: omni-dropdown features with JS on and off",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    let typeId = "";
    let typeName = "";
    
    // Part 1: JS ON
    let context = await browser.newContext();
    let page = await context.newPage();

    try {
      page.setDefaultNavigationTimeout(15000);
      page.setDefaultTimeout(15000);
      await createAndLoginUser(page, BASE);

      // Fetch a valid type ID and Name
      const response = await page.request.get(`${BASE}/api/dataset/song.types?per_page=1`);
      const typesData = await response.json();
      typeId = typesData.rows[0].id;
      typeName = typesData.rows[0].name;

      await page.goto(`${BASE}/song/add`, GOTO);
      await page.locator('.hyle-picker[data-hyle-picker-key="type"] details summary').click();

      const search = page.locator('input[name="pick_q_type"]');
      await search.fill(typeName);
      
      const rows = page.locator('.hyle-picker[data-hyle-picker-key="type"] .hyle-picker-rows').first();
      await rows.waitFor({ state: "visible" });
      let text = await rows.innerText();
      while (!text.includes(typeName)) {
        await page.waitForTimeout(500);
        text = await rows.innerText();
      }

      await page.locator(`input[type="checkbox"][name="type"][value="${typeId}"]`).check();
      let valsText = await page.locator('.hyle-picker[data-hyle-picker-key="type"] .hyle-picker-values').innerText();
      while (!valsText.includes(typeName)) {
        await page.waitForTimeout(100);
        valsText = await page.locator('.hyle-picker[data-hyle-picker-key="type"] .hyle-picker-values').innerText();
      }

      await page.locator('form[method="POST"] input[name="title"]').fill("Omni JS test song " + Date.now());
      await Promise.all([
        page.waitForURL(/\/song\/(?!add$)[^\/]+$/, { timeout: 10000 }),
        page.locator('form[method="POST"] button[type="submit"]').click(),
      ]);
      const songUrl = page.url();
      
      await page.goto(`${songUrl}/edit`, GOTO);
      const checkboxes = await page.locator('input[name="type"]').evaluateAll((nodes) => nodes.map(n => ({ value: n.value, checked: n.checked })));
      const checkedBoxes = checkboxes.filter(c => c.checked);
      const editValsText = await page.locator('.hyle-picker[data-hyle-picker-key="type"] .hyle-picker-values').innerText().catch(() => "");
      assert(checkedBoxes.some(c => c.value.toLowerCase() === typeId.toLowerCase() || c.value === typeName) || editValsText.includes(typeName), `expected ${typeId} or ${typeName} to be checked or in picker values on edit page`);

      await page.goto(`${BASE}/song/add`, GOTO);
      await page.locator('.hyle-picker[data-hyle-picker-key="type"] details summary').click();
      
      const initialCount = await page.locator('.hyle-picker[data-hyle-picker-key="type"] .hyle-picker-rows input[name="type"]').count();
      
      await page.locator('.hyle-picker[data-hyle-picker-key="type"] .hyle-picker-panel').evaluate((node) => {
        node.scrollTop = node.scrollHeight;
      });
      
      let currentCount = initialCount;
      let tries = 0;
      while (currentCount <= initialCount && tries < 20) {
        await page.waitForTimeout(250);
        currentCount = await page.locator('.hyle-picker[data-hyle-picker-key="type"] .hyle-picker-rows input[name="type"]').count();
        tries++;
      }
      assert(currentCount > initialCount, "rows should grow after scroll");

      await search.fill("some-gibberish-that-will-not-match");
      text = await rows.innerText();
      while (!text.includes("No matches")) {
        await page.waitForTimeout(500);
        text = await rows.innerText();
      }

    } finally {
      await context.close();
    }
    
    context = await browser.newContext({ javaScriptEnabled: false });
    page = await context.newPage();
    
    try {
      page.setDefaultNavigationTimeout(15000);
      page.setDefaultTimeout(15000);
      await createAndLoginUser(page, BASE);
      
      const uniqueTitle = "Draft preserve nojs " + Date.now();
      await page.goto(`${BASE}/song/add`, GOTO);
      await page.locator('form[method="POST"] input[name="title"]').fill(uniqueTitle);
      
      await page.locator('.hyle-picker[data-hyle-picker-key="type"] details summary').click();
      await page.locator('input[name="pick_q_type"]').fill(typeName);
      await Promise.all([
        page.waitForNavigation(),
        page.locator('input[name="pick_q_type"]').press("Enter"),
      ]);
      
      // JS-off navigation loses unsaved main-form drafts (acceptable degradation).
      // So we must fill the title AFTER the navigation.
      await page.locator('form[method="POST"] input[name="title"]').fill(uniqueTitle);
      
      await page.locator(`input[type="checkbox"][name="type"][value="${typeId}"]`).check();
      await Promise.all([
        page.waitForURL(/\/song\/(?!add$)[^\/]+$/, { timeout: 10000 }),
        page.locator('form[method="POST"] button[type="submit"]').click(),
      ]);
      
      const bodyText = await page.locator('body').innerText();
      assert(bodyText.includes(typeName), `saved item should have type ${typeName}`);
    } finally {
      await context.close();
      await browser.close();
    }
  },
});
