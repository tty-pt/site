/** PROBE (regression guard): verify the exact POST body for a multi type add
 * carries BOTH selected values even when the second selection is made via
 * search — which swaps the rows panel and wipes the first selection's row
 * from the DOM. Guards hyle-fragments.js submit-time reconciliation.
 * Run against the clean instrumented :8080 with: deno test --no-check.
 */
import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const SLUG = (v: string) => v.toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_+|_+$/g, "");

function assert(c: boolean, m: string): void {
  if (!c) throw new Error(m);
}

function fieldValues(body: string, name: string): string[] {
  const out: string[] = [];
  for (const m of body.matchAll(new RegExp(`name="${name}"[\\s\\S]*?\\r\\n\\r\\n([^\\r\\n]+)`, "g"))) {
    out.push(m[1]);
  }
  return out;
}

Deno.test({
  name: "PROBE: multi type add POSTs both values after search-driven select",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();
    let songId: string | null = null;
    try {
      page.setDefaultTimeout(10000);
      page.setDefaultNavigationTimeout(15000);
      await createAndLoginUser(page, BASE);
      const ts = Date.now();
      await page.goto(`${BASE}/song/add`, { waitUntil: "domcontentloaded" });
      await page.waitForSelector('input[name="title"]', { timeout: 5000 });
      await page.fill('input[name="title"]', `ProbePost ${ts}`);

      const picker = page.locator('.hyle-picker[data-hyle-picker-key="type"]');
      const typeDetails = picker.locator('details.hyle-picker-details');
      if ((await typeDetails.count()) > 0 && (await typeDetails.first().getAttribute('open')) === null) {
        await typeDetails.locator('summary').first().click();
      }

      const tick = (slug: string) =>
        picker.locator(`input[name="type"][value="${slug}"]`).first().evaluate((el) => {
          (el as HTMLInputElement).checked = true;
          el.dispatchEvent(new Event("change", { bubbles: true }));
        });

      // 1. tick Communion from the rows (or search if on later page)
      if (await picker.locator('input[name="type"][value="communion"]').count() === 0) {
        const searchInit = picker.locator('input.hyle-picker-search');
        await searchInit.fill("Communion");
        for (let i = 0; i < 30; i++) {
          if (await picker.locator('input[name="type"][value="communion"]').count() > 0) break;
          await page.waitForTimeout(100);
        }
      }
      assert(
        await picker.locator('input[name="type"][value="communion"]').count() > 0,
        "communion option must be reachable",
      );
      await tick("communion");
      await page.waitForTimeout(200);

      // 2. search "Entry" — this refetches and REPLACES the rows panel,
      //    wiping Communion's row from the DOM.
      const search = picker.locator('input.hyle-picker-search');
      await search.fill("Entry");
      const rows = picker.locator('.hyle-picker-rows').first();
      await rows.waitFor({ state: "visible" });
      let text = "";
      for (let i = 0; i < 20; i++) {
        text = await rows.innerText().catch(() => "");
        if (text.includes("Entry")) break;
        await page.waitForTimeout(100);
      }
      assert(text.includes("Entry"), `search should surface Entry, got: "${text.slice(0, 120)}"`);
      await tick("entry");
      await page.waitForTimeout(200);

      // 3. capture the actual POST body on submit
      let rawBody = "";
      const onRequest = (r: import("npm:playwright").Request) => {
        if (r.method() === "POST" && r.url().endsWith("/song/add")) {
          const data = r.postData() || "";
          if (data.includes("name=\"type\"")) rawBody = data;
        }
      };
      page.on("request", onRequest);

      const [response] = await Promise.all([
        page.waitForURL(/\/song\/[^/]+$/, { timeout: 10000 }),
        page.click('form[method="POST"] button[type="submit"]'),
      ]);
      page.off("request", onRequest);
      songId = page.url().replace(`${BASE}/song/`, "").replace(/\/$/, "");

      assert(rawBody.length > 0, "captured POST /song/add body with type fields");
      const types = fieldValues(rawBody, "type");
      console.log(`\n══ captured submit body — type values: ${JSON.stringify(types)}`);
      assert(
        types.includes("communion") && types.includes("entry"),
        `POSTed type must include BOTH communion and entry, got: ${JSON.stringify(types)}`,
      );
    } finally {
      await browser.close();
      if (songId) {
        try {
          await Deno.remove(`var/song/${songId}`, { recursive: true });
        } catch { /* ignore */ }
      }
    }
  },
});