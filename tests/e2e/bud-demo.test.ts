import { chromium } from "npm:playwright";
import { expect } from "npm:@playwright/test";

const BASE = "http://localhost:8080";

Deno.test("bud macro and hydration", async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await page.goto(`${BASE}/bud-demo`);
    
    // Check SSR rendered output
    const title = page.locator('h1');
    await expect(title).toHaveText('Interactive C WASM Demo');
    
    const button = page.locator('button');
    await expect(button).toHaveText('Count: 0');
    
    // Let WASM load and hydrate
    await page.waitForLoadState('networkidle');
    await page.waitForTimeout(300);
    
    // Click and verify hydration
    await button.click();
    await expect(button).toHaveText('Count: 1');
    
    await button.click();
    await expect(button).toHaveText('Count: 2');
  } finally {
    await browser.close();
  }
});
