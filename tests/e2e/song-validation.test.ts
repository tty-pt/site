/**
 * E2E test: song validation at write time
 *
 * Verifies that POST/PUT to /api/dataset/songs/ without a required
 * title field returns 422 with field-level error details.
 *
 * Requires: axil running on :8080.
 * Requires: AUTH_SKIP_CONFIRM=1 in the test process environment.
 */

import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";
import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";
const DATASET = "song.items";

function assert(cond: boolean, msg: string): void {
  if (!cond) throw new Error(msg);
}

Deno.test({
  name: "song POST: missing required title returns 422",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();
    try {
      const user = await createAndLoginUser(page, BASE);

      // Get cookies from the browser session
      const cookies = await page.context().cookies();
      const cookieStr = cookies
        .map((c: { name: string; value: string }) =>
          `${c.name}=${c.value}`
        )
        .join("; ");

      // Get CSRF token
      const { token: csrf, cookieHeader: ch } =
        await getCsrfToken(cookieStr, BASE);

      // POST without title (missing required field)
      const body = new URLSearchParams();
      body.set("author", "Test Author");
      body.set("type", "1");
      body.set("csrf_token", csrf);

      const resp = await fetch(`${BASE}/api/dataset/${DATASET}/`, {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
          Cookie: ch,
        },
        body: body.toString(),
        redirect: "manual",
      });

      assert(resp.status === 422,
        `Expected 422, got ${resp.status}`);

      const json = await resp.json();
      assert(json.errors !== undefined, "response has errors array");
      assert(json.errors.length >= 1, "at least one error");

      const titleErr = json.errors.find(
        (e: { field: string }) => e.field === "title",
      );
      assert(titleErr !== undefined,
        "one error is for field 'title'");
      assert(titleErr.rule === "required",
        "title error rule is 'required'");
    } finally {
      await browser.close();
    }
  },
});

Deno.test({
  name: "song POST: with title succeeds",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();
    try {
      const user = await createAndLoginUser(page, BASE);

      const cookies = await page.context().cookies();
      const cookieStr = cookies
        .map((c: { name: string; value: string }) =>
          `${c.name}=${c.value}`
        )
        .join("; ");

      const { token: csrf, cookieHeader: ch } =
        await getCsrfToken(cookieStr, BASE);

      // POST with title (should succeed)
      const body = new URLSearchParams();
      body.set("title", "Test Song Validation");
      body.set("author", "Test Author");
      body.set("type", "1");
      body.set("csrf_token", csrf);

      const resp = await fetch(`${BASE}/api/dataset/${DATASET}/`, {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
          Cookie: ch,
        },
        body: body.toString(),
        redirect: "manual",
      });

      assert(resp.status === 201,
        `Expected 201, got ${resp.status}`);

      const json = await resp.json();
      assert(json.id !== undefined, "response has id");
    } finally {
      await browser.close();
    }
  },
});
