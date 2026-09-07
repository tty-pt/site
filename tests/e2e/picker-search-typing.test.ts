/**
 * E2E test verifying that characters typed into a picker's search input
 * while a search fetch is in-flight are NOT lost when the response arrives.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const GOTO = { waitUntil: "domcontentloaded" as const };

function assert(condition: boolean, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

Deno.test({
  name: "picker search: characters typed during in-flight fetch are preserved",
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

      // Navigate to song/add which has the 'type' picker
      await page.goto(`${BASE}/song/add`, GOTO);

      const pickerDetails = page.locator('.hyle-picker[data-hyle-picker-key="type"] details.hyle-picker-details');
      await pickerDetails.locator('summary').click();

      const searchInput = pickerDetails.locator('input.hyle-picker-search');
      await searchInput.waitFor({ state: "visible" });

      let resolveFirstFetch: (() => void) | null = null;
      let firstFetchStarted = false;

      // Intercept the /pick/ route to hold the first search fetch in-flight
      await page.route("**/pick/**", async (route) => {
        const url = route.request().url();
        if (!firstFetchStarted && url.includes("pick_q_type=r")) {
          firstFetchStarted = true;
          // Hold the request until the test types additional characters
          await new Promise<void>((resolve) => {
            resolveFirstFetch = resolve;
          });
        }
        await route.continue();
      });

      // Type the initial character to start a fetch
      await searchInput.fill("r");

      // Wait until the debounce expires and the fetch is in flight
      const startWait = Date.now();
      while (!firstFetchStarted && Date.now() - startWait < 5000) {
        await page.waitForTimeout(50);
      }
      assert(firstFetchStarted, "first search fetch should have started");

      // While the fetch is in flight, type additional characters
      await searchInput.focus();
      await page.keyboard.type("ock");

      const typedValueBeforeResponse = await searchInput.inputValue();
      assert(
        typedValueBeforeResponse === "rock",
        `expected input value to be "rock" while fetch is in-flight, got "${typedValueBeforeResponse}"`
      );

      // Now release the held fetch response
      const release = resolveFirstFetch as (() => void) | null;
      if (release) {
        release();
      }

      // Wait for the in-flight response to be received and processed by hyle-fragments.js
      await page.waitForTimeout(400);

      // Verify that characters typed during the in-flight fetch were NOT overwritten/lost
      const valueAfterResponse = await searchInput.inputValue();
      assert(
        valueAfterResponse === "rock",
        `Characters typed during in-flight fetch were lost! Expected "rock", got "${valueAfterResponse}"`
      );

      // Also verify caret/selection is preserved at the end of the typed string
      const caret = await searchInput.evaluate((el: any) => el.selectionStart);
      assert(
        caret === 4,
        `Expected caret position to be 4 (end of "rock"), got ${caret}`
      );

      // Verify input still has focus
      const isFocused = await searchInput.evaluate((el: any) => {
        const doc = (globalThis as any).document;
        return el === doc?.activeElement;
      });
      assert(isFocused, "Search input should retain focus after panel update");

      // Part 2: End-to-end typing with a valid dynamic dataset type
      const typesResp = await page.request.get(`${BASE}/api/dataset/song.types?per_page=10`);
      const typesData = await typesResp.json();
      const targetRow = typesData.rows.find((r: { id: string; name: string }) => r.id && r.name && r.name.length >= 3) || typesData.rows[0];
      const targetName = targetRow.name;
      const targetPrefix = targetName.slice(0, 1);
      const targetSuffix = targetName.slice(1);

      let resolveSuffixFetch: (() => void) | null = null;
      let suffixFetchStarted = false;

      await page.unroute("**/pick/**");
      await page.route("**/pick/**", async (route) => {
        const url = route.request().url();
        // Only intercept the prefix query
        if (!suffixFetchStarted && url.includes(`pick_q_type=${encodeURIComponent(targetPrefix)}&`)) {
          suffixFetchStarted = true;
          await new Promise<void>((resolve) => {
            resolveSuffixFetch = resolve;
          });
        }
        await route.continue();
      });

      await searchInput.fill(targetPrefix);

      const waitStart = Date.now();
      while (!suffixFetchStarted && Date.now() - waitStart < 5000) {
        await page.waitForTimeout(50);
      }
      assert(suffixFetchStarted, `search fetch for '${targetPrefix}' should have started`);

      // While fetch is in flight, type the rest of the target name
      await searchInput.focus();
      await page.keyboard.type(targetSuffix);

      assert(
        await searchInput.inputValue() === targetName,
        `expected input to be '${targetName}' while fetch in flight`
      );

      // Release first fetch
      const releaseSuffix = resolveSuffixFetch as (() => void) | null;
      if (releaseSuffix) {
        releaseSuffix();
      }

      // Wait and verify input remains targetName
      await page.waitForTimeout(400);
      assert(
        await searchInput.inputValue() === targetName,
        `expected input to remain '${targetName}' after first response`
      );

      // Wait for debounced search results for targetName to populate
      const rows = pickerDetails.locator('.hyle-picker-rows').first();
      await rows.waitFor({ state: "visible" });
      let text = await rows.innerText();
      const waitRows = Date.now();
      while (!text.includes(targetName) && Date.now() - waitRows < 5000) {
        await page.waitForTimeout(200);
        text = await rows.innerText();
      }
      assert(text.includes(targetName), `expected rows to contain '${targetName}'`);

      // Check the target option and verify values summary updates
      const targetOption = pickerDetails.locator(`.hyle-picker-option:has(input[value="${targetRow.id}"])`);
      await targetOption.locator('input[type="checkbox"]').check();
      const values = pickerDetails.locator('.hyle-picker-values');
      let valText = await values.innerText();
      while (!valText.includes(targetName)) {
        await page.waitForTimeout(100);
        valText = await values.innerText();
      }
      assert(valText.includes(targetName), `expected picker values to include '${targetName}'`);

    } finally {
      await context.close();
      await browser.close();
    }
  },
});
