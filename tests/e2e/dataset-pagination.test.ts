/**
 * Authenticated dataset API pagination normalization and query preservation.
 *
 * Requires: axil running on :8080.
 */

import { chromium, type Page } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const DATASET_URL = `${BASE}/api/dataset/song.items`;
const DEFAULT_PER_PAGE = 25;
const MAX_PER_PAGE = 100;

interface DatasetResponse {
  rows: Array<{ id: string; title?: string }>;
  pagination: {
    page: number;
    per_page: number;
    total: number;
    total_pages: number;
  };
}

function assert(condition: boolean, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

async function getDataset(page: Page, query: string): Promise<DatasetResponse> {
  const response = await page.request.get(`${DATASET_URL}?${query}`);
  assert(response.status() === 200, `GET failed with ${response.status()}`);
  return await response.json() as DatasetResponse;
}

Deno.test({
  name: "dataset API: normalizes pagination in metadata and hyle query",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const context = await browser.newContext();
    const page = await context.newPage();

    try {
      await createAndLoginUser(page, BASE);

      const first = await getDataset(page, "page=1&per_page=5");
      const firstIds = first.rows.map((row) => row.id);
      assert(firstIds.length > 0, "expected songs in the test dataset");

      for (const value of ["0", "-1", "nope"]) {
        const data = await getDataset(page, `per_page=${value}`);
        assert(
          data.pagination.per_page === DEFAULT_PER_PAGE,
          `per_page=${value} should report the default`,
        );
        assert(
          data.rows.length ===
            Math.min(DEFAULT_PER_PAGE, data.pagination.total),
          `per_page=${value} should also limit hyle to the default`,
        );
      }

      for (const value of ["101", "999999999999999999999999"]) {
        const data = await getDataset(page, `per_page=${value}`);
        assert(
          data.pagination.per_page === MAX_PER_PAGE,
          `per_page=${value} should report the cap`,
        );
        assert(
          data.rows.length === Math.min(MAX_PER_PAGE, data.pagination.total),
          `per_page=${value} should also limit hyle to the cap`,
        );
      }

      for (
        const value of [
          "0",
          "-2",
          "bad",
          "42949673",
          "999999999999999999999999",
        ]
      ) {
        const data = await getDataset(page, `page=${value}&per_page=5`);
        assert(
          data.pagination.page === 1,
          `page=${value} should report page 1`,
        );
        assert(
          data.rows.map((row) => row.id).join("\n") === firstIds.join("\n"),
          `page=${value} should query hyle page 1`,
        );
      }
    } finally {
      await browser.close();
    }
  },
});

Deno.test({
  name: "dataset API: preserves include and repeated filters when paginating",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const context = await browser.newContext();
    const page = await context.newPage();

    try {
      await createAndLoginUser(page, BASE);
      const communion = await getDataset(page, "type=comunhao&per_page=100");
      const christmas = await getDataset(page, "type=natal&per_page=100");
      const data = await getDataset(
        page,
        "type=comunhao&type=natal&type_op=or&include=title&page=1&per_page=1000",
      );

      assert(
        data.pagination.per_page === MAX_PER_PAGE,
        "expected capped metadata",
      );
      assert(data.rows.length > 0, "expected repeated filters to return songs");
      assert(
        data.pagination.total > communion.pagination.total &&
          data.pagination.total > christmas.pagination.total,
        "expected both repeated type filters to contribute results",
      );
      assert(
        data.rows.length === Math.min(MAX_PER_PAGE, data.pagination.total),
        "expected the rewritten query to retain the cap",
      );
      for (const row of data.rows) {
        assert(
          typeof row.title === "string",
          `include lost title for ${row.id}`,
        );
      }
    } finally {
      await browser.close();
    }
  },
});
