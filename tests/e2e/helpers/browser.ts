/**
 * Shared Browser and Context Lifecycle Management for E2E Tests.
 *
 * Reuses a single Chromium browser instance per Deno worker process and creates
 * fast, isolated BrowserContexts for each test to eliminate process launch overhead.
 */

import { chromium, type Browser, type BrowserContext, type Page } from "npm:playwright";

let sharedBrowser: Browser | null = null;
let cleanupRegistered = false;

/**
 * Get or initialize the shared Chromium browser instance for the current process.
 */
export async function getBrowser(): Promise<Browser> {
  if (!sharedBrowser || !sharedBrowser.isConnected()) {
    sharedBrowser = await chromium.launch({
      args: ["--no-sandbox", "--disable-setuid-sandbox", "--disable-dev-shm-usage"],
    });
    if (!cleanupRegistered) {
      cleanupRegistered = true;
      globalThis.addEventListener("unload", async () => {
        if (sharedBrowser) {
          await sharedBrowser.close().catch(() => {});
          sharedBrowser = null;
        }
      });
    }
  }
  return sharedBrowser;
}

/**
 * Execute a test function within a clean, isolated BrowserContext and Page.
 * Automatically closes the context when the test finishes.
 */
export async function withTestContext<T>(
  fn: (context: BrowserContext, page: Page) => Promise<T>,
  options?: Parameters<Browser["newContext"]>[0]
): Promise<T> {
  const browser = await getBrowser();
  const context = await browser.newContext(options);
  const page = await context.newPage();
  try {
    return await fn(context, page);
  } finally {
    await context.close().catch(() => {});
  }
}

/**
 * Execute a test function with a fresh isolated Page.
 */
export async function withTestPage<T>(
  fn: (page: Page) => Promise<T>,
  options?: Parameters<Browser["newContext"]>[0]
): Promise<T> {
  return withTestContext((_context, page) => fn(page), options);
}

/**
 * Explicitly close the shared browser instance if needed.
 */
export async function closeSharedBrowser(): Promise<void> {
  if (sharedBrowser) {
    await sharedBrowser.close().catch(() => {});
    sharedBrowser = null;
  }
}
