/**
 * E2E test verifying multi-reference picker checkbox layout and styling.
 * Ensures checkboxes inside .hyle-picker-option do not stretch or expand
 * across the container (no flex-grow, width is compact/auto, not 100%).
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const GOTO = { waitUntil: "domcontentloaded" as const };

function assert(condition: boolean, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

Deno.test({
  name: "picker checkbox layout: multi-reference picker checkboxes render compactly without flex grow",
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

      // Navigate to song/add which contains the multi-reference type picker
      await page.goto(`${BASE}/song/add`, GOTO);

      const pickerSummary = page.locator('.hyle-picker[data-hyle-picker-key="type"] details.hyle-picker-details summary');
      await pickerSummary.click();

      const options = page.locator('.hyle-picker[data-hyle-picker-key="type"] .hyle-picker-rows .hyle-picker-option');
      await options.first().waitFor({ state: "visible" });

      const count = await options.count();
      assert(count > 0, "expected at least one picker option in song type picker");

      // Inspect checkbox elements inside the picker options
      const checkboxEvaluations = await options.evaluateAll((elements: any[]) => {
        return elements.map((el) => {
          const input = el.querySelector('input[type="checkbox"]');
          if (!input) return null;

          const win = (globalThis as any).window || globalThis;
          const inputComputed = win.getComputedStyle(input);
          const optionComputed = win.getComputedStyle(el);
          const inputRect = input.getBoundingClientRect();
          const optionRect = el.getBoundingClientRect();

          return {
            inputType: input.type,
            flexGrow: inputComputed.flexGrow,
            flexShrink: inputComputed.flexShrink,
            inputWidthPx: inputRect.width,
            optionWidthPx: optionRect.width,
            optionDisplay: optionComputed.display,
            optionFlexDirection: optionComputed.flexDirection,
          };
        }).filter(Boolean);
      });

      assert(checkboxEvaluations.length > 0, "expected checkbox inputs inside multi-reference picker options");

      for (const info of checkboxEvaluations) {
        if (!info) continue;
        assert(
          info.flexGrow === "0",
          `expected checkbox flex-grow to be "0", got "${info.flexGrow}"`
        );
        assert(
          info.inputWidthPx < 40,
          `expected checkbox width to be compact (< 40px), got ${info.inputWidthPx}px (option width is ${info.optionWidthPx}px)`
        );
        assert(
          info.inputWidthPx < info.optionWidthPx / 2,
          `checkbox should not take up entire option width: input=${info.inputWidthPx}px, option=${info.optionWidthPx}px`
        );
      }
    } finally {
      await context.close();
      await browser.close();
    }
  },
});
