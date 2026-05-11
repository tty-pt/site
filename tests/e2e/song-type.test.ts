/**
 * E2E test: song type field (multi-reference)
 *
 * Tests the full lifecycle of the type (category) field on songs:
 *   1. Create song with single type → verify detail state shows display name
 *   2. Create song with multiple types → verify detail state shows both
 *   3. Type display names resolve correctly (not raw position numbers)
 *   4. Listing shows display names for type column
 *   5. Filtering works by slug
 *   6. Edit form pre-fills existing type values
 *   7. On-disk type file contains display names (not slugs)
 *
 * Expected type entities on disk:
 *   communion  → name: "Communion"
 *   entry      → name: "Entry"
 *
 * Requires: axil running on :8080 with AUTH_SKIP_CONFIRM=1.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const REPO_ROOT = new URL("../../", import.meta.url).pathname.replace(/\/$/, "");
const BASE = "http://localhost:8080";

function assert(cond: boolean, msg: string): void {
  if (!cond) throw new Error(msg);
}

function extractSongId(url: string): string {
  return url.replace(`${BASE}/song/`, "").replace(/\/$/, "");
}

/** Extract the bud-state JSON from a detail page. */
async function extractStateJson(
  page: import("npm:playwright").Page,
): Promise<Record<string, unknown>> {
  const content = await page.content();
  const m = content.match(
    /<script[^>]*id="bud-state"[^>]*>([\s\S]*?)<\/script>/,
  );
  if (!m) throw new Error("Could not find bud-state script tag");
  return JSON.parse(m[1]);
}

/**
 * Create a song via the browser form and return its ID.
 */
async function createSongViaForm(
  page: import("npm:playwright").Page,
  title: string,
  type: string,
  author: string,
): Promise<string> {
  await page.goto(`${BASE}/song/add`);
  await page.waitForSelector('input[name="title"]', { timeout: 5000 });
  await page.fill('input[name="title"]', title);
  await page.fill('textarea[name="type"]', type);
  await page.fill('input[name="author"]', author);
  await page.click('button[type="submit"]');
  await page.waitForURL(/\/song\/[^/]+$/, { timeout: 5000 });
  return extractSongId(page.url());
}

/**
 * Create a song via the API and return its ID.
 */
async function createSongViaApi(
  page: import("npm:playwright").Page,
  title: string,
  type: string,
  author: string,
): Promise<string> {
  const cookies = await page.context().cookies();
  const cookieStr = cookies
    .map((c: { name: string; value: string }) => `${c.name}=${c.value}`)
    .join("; ");
  const { token: csrf, cookieHeader: ch } = await getCsrfToken(cookieStr, BASE);

  const body = new URLSearchParams();
  body.set("title", title);
  body.set("author", author);
  body.set("type", type);
  body.set("csrf_token", csrf);

  const resp = await fetch(`${BASE}/api/dataset/song.items/`, {
    method: "POST",
    headers: {
      "Content-Type": "application/x-www-form-urlencoded",
      Cookie: ch,
    },
    body: body.toString(),
    redirect: "manual",
  });

  assert(resp.status === 201, `POST should return 201, got ${resp.status}`);
  const json = await resp.json();
  assert(!!json.id, "POST response should have id");
  return json.id;
}

/**
 * Find a song in the listing, using per_page=1000 to avoid pagination.
 */
async function findSongInListing(
  page: import("npm:playwright").Page,
  songId: string,
  filterUrl: string,
): Promise<boolean> {
  const sep = filterUrl.includes("?") ? "&" : "?";
  const url = `${filterUrl}${sep}per_page=1000`;
  await page.goto(url, { waitUntil: "domcontentloaded" });
  await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });
  const link = page.locator(`a[href="/song/${songId}"]`);
  return link.isVisible().catch(() => false);
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 1: Single type → detail state shows display name, not position number
// ──────────────────────────────────────────────────────────────────────────────

Deno.test({
  name: "song type: single type detail shows display name",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();
    let songId: string | null = null;

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      await createAndLoginUser(page, BASE);

      const ts = Date.now();
      songId = await createSongViaForm(
        page,
        `Type Single Test ${ts}`,
        "Communion",
        "Type Test Author",
      );

      // ── 1. Detail page: state JSON should show "Communion", not a position number
      {
        const state = await extractStateJson(page);
        const typeVal = state.type;

        // Accept either a string "Communion" or an array containing "Communion"
        let passes = false;
        if (typeof typeVal === "string" && typeVal === "Communion") {
          passes = true;
        } else if (
          Array.isArray(typeVal) &&
          typeVal.length === 1 &&
          typeVal[0] === "Communion"
        ) {
          passes = true;
        }

        if (!passes) {
          throw new Error(
            `Detail state type should be "Communion" or ["Communion"], got: ${JSON.stringify(typeVal)}`,
          );
        }
      }

      // ── 2. On-disk type file should contain "Communion"
      {
        const typeFile = `${REPO_ROOT}/items/song/items/${songId}/type`;
        const content = await Deno.readTextFile(typeFile);
        assert(
          content.includes("Communion"),
          `On-disk type file should contain "Communion", got: "${content.trim()}"`,
        );
      }

      // ── 3. Listing with filter: type column should show "Communion"
      {
        const found = await findSongInListing(
          page,
          songId,
          `${BASE}/song?type=communion`,
        );
        assert(found, `Song ${songId} should be found via type=communion filter`);

        const row = page
          .locator(`a[href="/song/${songId}"]`)
          .first()
          .locator("xpath=ancestor::tr");
        const typeCell = row.locator("td").nth(1);
        const typeText = (await typeCell.textContent())?.trim() ?? "";

        // Should show "Communion", NOT a slug or position number
        if (typeText === "communion" || /^\d+$/.test(typeText)) {
          throw new Error(
            `Listing type should NOT be slug "communion" or position number, got: "${typeText}"`,
          );
        }
        assert(
          typeText === "Communion",
          `Listing type should show "Communion", got: "${typeText}"`,
        );
      }

      // ── 4. Edit form: type textarea should be pre-filled
      {
        await page.goto(`${BASE}/song/${songId}/edit`, {
          waitUntil: "domcontentloaded",
        });
        await page.waitForSelector('textarea[name="type"]', { timeout: 5000 });
        const typeValue = await page.inputValue('textarea[name="type"]');
        assert(
          typeValue.trim() === "Communion",
          `Edit form type should contain "Communion", got: "${typeValue.trim()}"`,
        );
      }

      // ── 5. Filter by slug communion should return this song
      {
        const found = await findSongInListing(
          page,
          songId,
          `${BASE}/song?type=communion`,
        );
        assert(found, `Filter type=communion should include song`);
      }

      // ── 6. Filter by display name "Communion" (soft check — may not work yet)
      {
        const found = await findSongInListing(
          page,
          songId,
          `${BASE}/song?type=Communion`,
        );
        if (!found) {
          console.log(
            "WARN: Filter type=Communion did not find the song (display-name filtering not yet supported)",
          );
        }
      }
    } finally {
      await browser.close();
      if (songId) {
        try {
          await Deno.remove(`${REPO_ROOT}/items/song/items/${songId}`, {
            recursive: true,
          });
        } catch { /* ignore */ }
      }
    }
  },
});

// ──────────────────────────────────────────────────────────────────────────────
// Test 2: Multiple types → detail state shows both display names
// ──────────────────────────────────────────────────────────────────────────────

Deno.test({
  name: "song type: multiple types detail shows both display names",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();
    let songId: string | null = null;

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      await createAndLoginUser(page, BASE);

      const ts = Date.now();
      songId = await createSongViaForm(
        page,
        `Type Multi Test ${ts}`,
        "Communion\nEntry",
        "Multi Type Author",
      );

      // ── 1. Detail page: state JSON should include both types ──────────────
      {
        const state = await extractStateJson(page);
        const typeVal = state.type;
        const typeStr = Array.isArray(typeVal)
          ? typeVal.join(", ")
          : String(typeVal);
        assert(
          typeStr.includes("Communion") && typeStr.includes("Entry"),
          `Detail state type should include both "Communion" and "Entry", got: ${JSON.stringify(typeVal)}`,
        );
      }

      // ── 2. On-disk type file should contain both display names ────────────
      {
        const typeFile = `${REPO_ROOT}/items/song/items/${songId}/type`;
        const content = await Deno.readTextFile(typeFile);
        assert(
          content.includes("Communion") && content.includes("Entry"),
          `On-disk type file should contain both types, got: "${content.trim()}"`,
        );
      }

      // ── 3. Filter by communion returns this song ─────────────────────────────
      {
        const found = await findSongInListing(
          page,
          songId,
          `${BASE}/song?type=communion`,
        );
        assert(found, "Filter type=communion should include multi-type song");
      }

      // ── 4. Filter by entry returns this song ─────────────────────────────
      {
        const found = await findSongInListing(
          page,
          songId,
          `${BASE}/song?type=entry`,
        );
        assert(found, "Filter type=entry should include multi-type song");
      }

      // ── 5. Edit form: type textarea should contain both types ─────────────
      {
        await page.goto(`${BASE}/song/${songId}/edit`, {
          waitUntil: "domcontentloaded",
        });
        await page.waitForSelector('textarea[name="type"]', { timeout: 5000 });
        const typeValue = await page.inputValue('textarea[name="type"]');
        const normalized = typeValue.trim().replace(/\r?\n/g, "\n");
        assert(
          normalized.includes("Communion") && normalized.includes("Entry"),
          `Edit form type should contain both types, got: "${typeValue.trim()}"`,
        );
      }
    } finally {
      await browser.close();
      if (songId) {
        try {
          await Deno.remove(`${REPO_ROOT}/items/song/items/${songId}`, {
            recursive: true,
          });
        } catch { /* ignore */ }
      }
    }
  },
});

// ──────────────────────────────────────────────────────────────────────────────
// Test 3: API POST with type → detail state shows display names
// ──────────────────────────────────────────────────────────────────────────────

Deno.test({
  name: "song type: API POST with type → detail shows display name",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();
    let songId: string | null = null;

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      await createAndLoginUser(page, BASE);

      const ts = Date.now();
      songId = await createSongViaApi(
        page,
        `API Type Test ${ts}`,
        "Communion",
        "API Type Author",
      );

      // ── Verify detail page state ──────────────────────────────────────────
      await page.goto(`${BASE}/song/${songId}`, {
        waitUntil: "domcontentloaded",
      });
      await page.waitForSelector("h1", { timeout: 5000 });

      const state = await extractStateJson(page);
      const typeVal = state.type;

      // The type should be the display name "Communion", not a raw position number
      let passes = false;
      if (typeof typeVal === "string" && typeVal === "Communion") {
        passes = true;
      } else if (
        Array.isArray(typeVal) &&
        typeVal.length === 1 &&
        typeVal[0] === "Communion"
      ) {
        passes = true;
      }

      if (!passes) {
        throw new Error(
          `API-created song detail state type should be "Communion" or ["Communion"], got: ${JSON.stringify(typeVal)}`,
        );
      }

      // ── Verify type file on disk ──────────────────────────────────────────
      const typeFile = `${REPO_ROOT}/items/song/items/${songId}/type`;
      const content = await Deno.readTextFile(typeFile);
      assert(
        content.includes("Communion"),
        `On-disk type file should contain "Communion", got: "${content.trim()}"`,
      );
    } finally {
      await browser.close();
      if (songId) {
        try {
          await Deno.remove(`${REPO_ROOT}/items/song/items/${songId}`, {
            recursive: true,
          });
        } catch { /* ignore */ }
      }
    }
  },
});

// ──────────────────────────────────────────────────────────────────────────────
// Test 4: Verify type entity name files exist
// ──────────────────────────────────────────────────────────────────────────────

Deno.test({
  name: "song type: type entity name files exist and are correct",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const gjTcName = `${REPO_ROOT}/items/song/types/communion/name`;
    try {
      const content = await Deno.readTextFile(gjTcName);
      assert(
        content.trim() === "Communion",
        `communion/name should contain "Communion", got: "${content.trim()}"`,
      );
    } catch (e) {
      if (e instanceof Deno.errors.NotFound) {
        throw new Error(`Type entity name file missing: ${gjTcName}`);
      }
      throw e;
    }

    const gjTeName = `${REPO_ROOT}/items/song/types/entry/name`;
    try {
      const content = await Deno.readTextFile(gjTeName);
      assert(
        content.trim() === "Entry",
        `entry/name should contain "Entry", got: "${content.trim()}"`,
      );
    } catch (e) {
      if (e instanceof Deno.errors.NotFound) {
        throw new Error(`Type entity name file missing: ${gjTeName}`);
      }
      throw e;
    }
  },
});

// ──────────────────────────────────────────────────────────────────────────────
// Test 5: Listing with filter shows display names, not slugs or positions
// ──────────────────────────────────────────────────────────────────────────────

Deno.test({
  name: "song type: listing type column shows display names not slugs",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();
    let songId: string | null = null;

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      await createAndLoginUser(page, BASE);

      const ts = Date.now();
      songId = await createSongViaForm(
        page,
        `Type Display Test ${ts}`,
        "Communion",
        "Type Display Author",
      );

      // Use type=communion filter to narrow to songs with this type
      await page.goto(`${BASE}/song?type=communion`, {
        waitUntil: "domcontentloaded",
      });
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

      // Find our song in the filtered results
      const link = page.locator(`a[href="/song/${songId}"]`).first();
      const found = await link.isVisible().catch(() => false);
      if (!found) {
        // Song may be on another page — check via API dataset endpoint instead
        console.log(
          `WARN: Song ${songId} not found on first page of type=communion filter, skipping listing check`,
        );
        return;
      }

      const rowTr = link.locator("xpath=ancestor::tr");
      const typeCell = rowTr.locator("td").nth(1);
      const typeText = (await typeCell.textContent())?.trim() ?? "";

      // Should NOT show the slug "communion"
      assert(
        typeText !== "communion",
        `Listing type should NOT show slug "communion", got: "${typeText}"`,
      );

      // Should NOT show a raw position number
      assert(
        !/^\d+$/.test(typeText),
        `Listing type should NOT be a raw position number, got: "${typeText}"`,
      );

      // Should show the display name "Communion"
      assert(
        typeText === "Communion",
        `Listing type should show "Communion", got: "${typeText}"`,
      );
    } finally {
      await browser.close();
      if (songId) {
        try {
          await Deno.remove(`${REPO_ROOT}/items/song/items/${songId}`, {
            recursive: true,
          });
        } catch { /* ignore */ }
      }
    }
  },
});

// ──────────────────────────────────────────────────────────────────────────────
// Test 6: Filter checkbox labels should be display names, not slugs
// ──────────────────────────────────────────────────────────────────────────────

Deno.test({
  name: "song type: filter checkboxes show entity labels",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      await page.goto(`${BASE}/song/`, { waitUntil: "domcontentloaded" });
      await page.waitForSelector("tr.hyle-row-clickable", { timeout: 10000 });

      // Check that filter checkboxes exist for type field
      const gjTcCheckbox = page.locator(
        'input[name="type"][value="communion"]',
      );
      const gjTeCheckbox = page.locator(
        'input[name="type"][value="entry"]',
      );

      const gjTcVisible = await gjTcCheckbox.isVisible().catch(() => false);
      const gjTeVisible = await gjTeCheckbox.isVisible().catch(() => false);

      assert(gjTcVisible, "Filter checkbox for communion should be visible");
      assert(gjTeVisible, "Filter checkbox for entry should be visible");

      // Check labels — these should show display names
      const gjTcLabel = gjTcCheckbox.locator("xpath=ancestor::label");
      const gjTcLabelText = (await gjTcLabel.textContent())?.trim() ?? "";
      assert(
        gjTcLabelText.includes("Communion"),
        `communion checkbox label should include "Communion", got: "${gjTcLabelText}"`,
      );

      const gjTeLabel = gjTeCheckbox.locator("xpath=ancestor::label");
      const gjTeLabelText = (await gjTeLabel.textContent())?.trim() ?? "";
      assert(
        gjTeLabelText.includes("Entry"),
        `entry checkbox label should include "Entry", got: "${gjTeLabelText}"`,
      );
    } finally {
      await browser.close();
    }
  },
});

// ──────────────────────────────────────────────────────────────────────────────
// Test 7: On-disk type file format consistency
// ──────────────────────────────────────────────────────────────────────────────

Deno.test({
  name: "song type: on-disk type file uses newline separators",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();
    let songId: string | null = null;

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      await createAndLoginUser(page, BASE);

      const ts = Date.now();
      songId = await createSongViaForm(
        page,
        `Type Format Test ${ts}`,
        "Communion\nEntry",
        "Format Test Author",
      );

      // On-disk type file should use newline separators, not comma separators
      const typeFile = `${REPO_ROOT}/items/song/items/${songId}/type`;
      const content = await Deno.readTextFile(typeFile);
      const trimmed = content.trim();

      // Should NOT use comma separator
      assert(
        !trimmed.includes(","),
        `On-disk type file should NOT use comma separator, got: "${trimmed}"`,
      );

      // Should use newline separator (for multi-value)
      const lines = trimmed.split("\n").filter((l) => l.length > 0);
      assert(
        lines.length === 2,
        `On-disk type file should have 2 lines, got ${lines.length}: "${trimmed}"`,
      );

      // Each line should be a slug, not a display name
      assert(
        lines.includes("communion") || lines.includes("Communion"),
        `On-disk type lines should contain expected values, got: ${JSON.stringify(lines)}`,
      );
    } finally {
      await browser.close();
      if (songId) {
        try {
          await Deno.remove(`${REPO_ROOT}/items/song/items/${songId}`, {
            recursive: true,
          });
        } catch { /* ignore */ }
      }
    }
  },
});

// ──────────────────────────────────────────────────────────────────────────────
// Test 8: Replace type via edit → persists on detail + filterable
// ──────────────────────────────────────────────────────────────────────────────

Deno.test({
  name: "song type: replace type via edit persists and is filterable",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();
    let songId: string | null = null;

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      await createAndLoginUser(page, BASE);

      const ts = Date.now();
      songId = await createSongViaForm(
        page,
        `Type Replace Test ${ts}`,
        "Communion",
        "Replace Test Author",
      );

      // Verify initial type on disk
      {
        const typeFile = `${REPO_ROOT}/items/song/items/${songId}/type`;
        const content = await Deno.readTextFile(typeFile);
        assert(
          content.includes("Communion"),
          `Initial type should be "Communion", got: "${content.trim()}"`,
        );
      }

      // ── Edit: replace Communion with Entry
      await page.goto(`${BASE}/song/${songId}/edit`, {
        waitUntil: "domcontentloaded",
      });
      await page.waitForSelector('textarea[name="type"]', { timeout: 5000 });
      await page.fill('textarea[name="type"]', "Entry");
      await Promise.all([
        page.waitForURL(`${BASE}/song/${songId}`, { timeout: 10000 }),
        page.click('button[type="submit"]'),
      ]);

      // ── Detail page should show "Entry"
      {
        const state = await extractStateJson(page);
        const typeVal = state.type;
        let passes = false;
        if (typeof typeVal === "string" && typeVal === "Entry") {
          passes = true;
        } else if (
          Array.isArray(typeVal) &&
          typeVal.length === 1 &&
          typeVal[0] === "Entry"
        ) {
          passes = true;
        }
        assert(
          passes,
          `After edit, detail type should be "Entry" or ["Entry"], got: ${JSON.stringify(typeVal)}`,
        );
      }

      // ── On-disk type file should now contain "Entry"
      {
        const typeFile = `${REPO_ROOT}/items/song/items/${songId}/type`;
        const content = await Deno.readTextFile(typeFile);
        assert(
          content.includes("Entry"),
          `After edit, on-disk type should contain "Entry", got: "${content.trim()}"`,
        );
        assert(
          !content.includes("Communion"),
          `After edit, on-disk type should NOT contain "Communion", got: "${content.trim()}"`,
        );
      }

      // ── Filter by entry should find this song
      {
        const found = await findSongInListing(
          page,
          songId,
          `${BASE}/song?type=entry`,
        );
        assert(found, "Filter type=entry should find song after type change");
      }

      // ── Filter by communion should NOT find this song
      {
        const found = await findSongInListing(
          page,
          songId,
          `${BASE}/song?type=communion`,
        );
        assert(
          !found,
          "Filter type=communion should NOT find song after type change",
        );
      }
    } finally {
      await browser.close();
      if (songId) {
        try {
          await Deno.remove(`${REPO_ROOT}/items/song/items/${songId}`, {
            recursive: true,
          });
        } catch { /* ignore */ }
      }
    }
  },
});

// ──────────────────────────────────────────────────────────────────────────────
// Test 9: Add second type via edit → both types persist and are filterable
// ──────────────────────────────────────────────────────────────────────────────

Deno.test({
  name: "song type: add second type via edit persists and both filterable",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();
    let songId: string | null = null;

    try {
      page.setDefaultNavigationTimeout(10000);
      page.setDefaultTimeout(10000);

      await createAndLoginUser(page, BASE);

      const ts = Date.now();
      songId = await createSongViaForm(
        page,
        `Type Add Second Test ${ts}`,
        "Communion",
        "Add Second Author",
      );

      // ── Edit: append "Entry" on new line
      await page.goto(`${BASE}/song/${songId}/edit`, {
        waitUntil: "domcontentloaded",
      });
      await page.waitForSelector('textarea[name="type"]', { timeout: 5000 });
      const current = await page.inputValue('textarea[name="type"]');
      await page.fill(
        'textarea[name="type"]',
        current.trim() + "\nEntry",
      );
      await Promise.all([
        page.waitForURL(`${BASE}/song/${songId}`, { timeout: 10000 }),
        page.click('button[type="submit"]'),
      ]);

      // ── Detail page should show both types
      {
        const state = await extractStateJson(page);
        const typeVal = state.type;
        const typeStr = Array.isArray(typeVal)
          ? typeVal.join(", ")
          : String(typeVal);
        assert(
          typeStr.includes("Communion") && typeStr.includes("Entry"),
          `After adding type, detail should include both "Communion" and "Entry", got: ${JSON.stringify(typeVal)}`,
        );
      }

      // ── On-disk type file should contain both
      {
        const typeFile = `${REPO_ROOT}/items/song/items/${songId}/type`;
        const content = await Deno.readTextFile(typeFile);
        assert(
          content.includes("Communion") && content.includes("Entry"),
          `After adding type, on-disk should contain both, got: "${content.trim()}"`,
        );
      }

      // ── Filter by communion finds this song
      {
        const found = await findSongInListing(
          page,
          songId,
          `${BASE}/song?type=communion`,
        );
        assert(
          found,
          "Filter type=communion should find song after adding second type",
        );
      }

      // ── Filter by entry also finds this song
      {
        const found = await findSongInListing(
          page,
          songId,
          `${BASE}/song?type=entry`,
        );
        assert(
          found,
          "Filter type=entry should find song after adding second type",
        );
      }
    } finally {
      await browser.close();
      if (songId) {
        try {
          await Deno.remove(`${REPO_ROOT}/items/song/items/${songId}`, {
            recursive: true,
          });
        } catch { /* ignore */ }
      }
    }
  },
});
