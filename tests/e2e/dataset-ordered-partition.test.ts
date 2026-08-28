/**
 * Authenticated dataset API ordered partition CRUD.
 *
 * Requires: axil running on :8080.
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

function assert(condition: boolean, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

Deno.test({
  name: "dataset API: ordered partition CRUD (GET, POST, PUT, DELETE)",
  sanitizeResources: false,
  sanitizeOps: false,
  async fn() {
    const browser = await chromium.launch();
    const page = await browser.newPage();

    try {
      await createAndLoginUser(page, BASE);

      // Create a test gig to use as the partition key
      const gigTitle = `Ordered Test Gig ${Date.now()}`;
      await page.goto(`${BASE}/gig/add`);
      await page.fill('input[name="title"]', gigTitle);
      await page.click('button[type="submit"]');
      await page.waitForURL(/\/gig\/[a-z0-9-]+/);

      const gigUrl = page.url();
      const gigId = gigUrl.split("/gig/")[1]?.split("?")[0]?.replace(/\/$/, "");
      assert(!!gigId, "Failed to determine gig ID from URL");

      // 1. GET initial ordered items (should be empty)
      const getRes1 = await page.request.get(
        `${BASE}/api/dataset/gig.songs/${gigId}/ordered`,
      );
      const text1 = await getRes1.text();
      assert(getRes1.status() === 200, `GET initial failed with ${getRes1.status()}: ${text1}`);
      const data1 = JSON.parse(text1);
      assert(data1.total === 0, `Expected total 0, got ${data1.total}`);
      assert(Array.isArray(data1.items), "Expected items array");
      assert(data1.items.length === 0, "Expected empty items");

      // 2. POST to append item to ordered partition
      const postRes = await page.request.post(
        `${BASE}/api/dataset/gig.songs/${gigId}/ordered`,
        {
          form: {
            song: "amazing",
            transpose: "0",
            format: "any",
          },
        },
      );
      assert(postRes.status() === 201, `POST failed with ${postRes.status()}`);
      const postData = await postRes.json();
      assert(postData.ok === true, "Expected ok: true");
      assert(postData.index === 0, `Expected index 0, got ${postData.index}`);

      // 3. GET to verify item added
      const getRes2 = await page.request.get(
        `${BASE}/api/dataset/gig.songs/${gigId}/ordered`,
      );
      assert(getRes2.status() === 200, `GET after add failed with ${getRes2.status()}`);
      const data2 = await getRes2.json();
      assert(data2.total === 1, `Expected total 1, got ${data2.total}`);
      assert(data2.items.length === 1, "Expected 1 item in array");
      assert(data2.items[0].song === "amazing", `Expected song amazing, got ${data2.items[0].song}`);
      assert(data2.items[0].transpose === "0", `Expected transpose 0, got ${data2.items[0].transpose}`);

      // 4. PUT to update item at index 0
      const putRes = await page.request.put(
        `${BASE}/api/dataset/gig.songs/${gigId}/ordered/0`,
        {
          form: {
            transpose: "2",
          },
        },
      );
      assert(putRes.status() === 200, `PUT failed with ${putRes.status()}`);
      const putData = await putRes.json();
      assert(putData.ok === true, "Expected ok: true");

      // 5. GET to verify update
      const getRes3 = await page.request.get(
        `${BASE}/api/dataset/gig.songs/${gigId}/ordered`,
      );
      assert(getRes3.status() === 200, `GET after update failed with ${getRes3.status()}`);
      const data3 = await getRes3.json();
      assert(data3.total === 1, `Expected total 1, got ${data3.total}`);
      assert(data3.items[0].transpose === "2", `Expected transpose 2, got ${data3.items[0].transpose}`);

      // 6. DELETE item at index 0
      const delRes = await page.request.delete(
        `${BASE}/api/dataset/gig.songs/${gigId}/ordered/0`,
      );
      assert(delRes.status() === 200, `DELETE failed with ${delRes.status()}`);
      const delData = await delRes.json();
      assert(delData.ok === true, "Expected ok: true");

      // 7. GET to verify deleted
      const getRes4 = await page.request.get(
        `${BASE}/api/dataset/gig.songs/${gigId}/ordered`,
      );
      assert(getRes4.status() === 200, `GET after delete failed with ${getRes4.status()}`);
      const data4 = await getRes4.json();
      assert(data4.total === 0, `Expected total 0, got ${data4.total}`);
      assert(data4.items.length === 0, "Expected empty items after delete");
    } finally {
      await browser.close();
    }
  },
});
