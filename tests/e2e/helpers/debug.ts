/**
 * E2E Test Debugging Helpers
 *
 * Provides utilities for capturing test context when failures occur.
 * All output is written to the debug/ directory.
 */

import { chromium, Page } from "npm:playwright";

const DEBUG_DIR = "../../debug/tests";

/**
 * Setup request/response logging for a page.
 * Logs are printed to stdout and will appear in test capture files.
 */
export function setupRequestLogging(page: Page): void {
  page.on("request", (req) => {
    const url = req.url();
    // Skip noisy resources
    if (url.includes(".css") || url.includes(".wasm") || url.includes(".js")) {
      return;
    }
    console.log(`[REQ] ${req.method()} ${url}`);
  });

  page.on("response", (res) => {
    const url = res.url();
    // Skip noisy resources
    if (url.includes(".css") || url.includes(".wasm") || url.includes(".js")) {
      return;
    }
    const status = res.status();
    const statusStr = status >= 400 ? `\x1b[31m${status}\x1b[0m` : status.toString();
    console.log(`[RES] ${statusStr} ${url.split("?")[0]}`);
  });

  page.on("console", (msg) => {
    if (msg.type() === "error") {
      console.log(`[CONSOLE ERROR] ${msg.text()}`);
    }
  });
}

/**
 * Dump the current page HTML to a file for post-mortem analysis.
 */
export async function dumpPageHtml(page: Page, label: string): Promise<string> {
  const html = await page.content();
  const timestamp = new Date().toISOString().replace(/[:.]/g, "-");
  const filename = `${label}_${timestamp}.html`;
  
  try {
    await Deno.mkdir(DEBUG_DIR, { recursive: true });
  } catch {
    // Directory may already exist
  }
  
  const filepath = `${DEBUG_DIR}/${filename}`;
  await Deno.writeTextFile(filepath, html);
  console.log(`[DEBUG] Page HTML saved to ${filepath}`);
  return filepath;
}

/**
 * Take a screenshot and save it to the debug directory.
 */
export async function takeScreenshot(page: Page, label: string): Promise<string> {
  const timestamp = new Date().toISOString().replace(/[:.]/g, "-");
  const filename = `${label}_${timestamp}.png`;
  
  try {
    await Deno.mkdir(DEBUG_DIR, { recursive: true });
  } catch {
    // Directory may already exist
  }
  
  const filepath = `${DEBUG_DIR}/${filename}`;
  const buffer = await page.screenshot();
  await Deno.writeFile(filepath, buffer);
  console.log(`[DEBUG] Screenshot saved to ${filepath}`);
  return filepath;
}

/**
 * Get a summary of the page state for debugging.
 */
export async function getPageState(page: Page): Promise<string> {
  const url = page.url();
  const title = await page.title();
  
  // Get visible text content (first 500 chars)
  const bodyText = await page.textContent("body");
  const textPreview = bodyText?.slice(0, 500).replace(/\s+/g, " ").trim() || "(empty)";
  
  return `
=== Page State ===
URL: ${url}
Title: ${title}
Body preview: ${textPreview}...
`;
}

/**
 * Wrapper that captures debug information on test failure.
 * Usage:
 *   await withDebugCapture(page, "songbook-edit-row", async () => {
 *     // test code here
 *   });
 */
export async function withDebugCapture(
  page: Page,
  testName: string,
  fn: () => Promise<void>
): Promise<void> {
  // Setup console capture
  const consoleLogs: string[] = [];
  page.on("console", (msg) => {
    consoleLogs.push(`[${msg.type().toUpperCase()}] ${msg.text()}`);
  });

  try {
    await fn();
  } catch (e) {
    // Capture debug info on failure
    console.log("\n=== TEST FAILURE DEBUG INFO ===");
    console.log(`Test: ${testName}`);
    console.log(`Timestamp: ${new Date().toISOString()}`);
    
    // Print console logs
    if (consoleLogs.length > 0) {
      console.log("\n--- Browser Console Logs ---");
      consoleLogs.forEach((log) => console.log(log));
    }
    
    // Get page state
    try {
      const state = await getPageState(page);
      console.log(state);
    } catch (stateErr) {
      console.log(`Could not get page state: ${stateErr}`);
    }
    
    // Take screenshot
    try {
      await takeScreenshot(page, testName);
    } catch (screenshotErr) {
      console.log(`Could not take screenshot: ${screenshotErr}`);
    }
    
    // Dump HTML
    try {
      await dumpPageHtml(page, testName);
    } catch (htmlErr) {
      console.log(`Could not dump HTML: ${htmlErr}`);
    }
    
    console.log("================================\n");
    throw e;
  }
}

/**
 * Simple test runner that sets up logging and runs a test.
 */
export async function runWithLogging(
  testName: string,
  testFn: (page: Page) => Promise<void>
): Promise<void> {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  
  setupRequestLogging(page);
  
  try {
    await withDebugCapture(page, testName, async () => {
      await testFn(page);
    });
  } finally {
    await browser.close();
  }
}