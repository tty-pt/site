import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test({
  name: "dataset api: generic write support (POST, PUT, DELETE)",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const cookieHeader = cookies.map((c) => `${c.name}=${c.value}`).join("; ");
    const { token: csrfToken, cookieHeader: ch } = await getCsrfToken(cookieHeader, BASE);

    // 1. POST (Create)
    const postResp = await fetch(`${BASE}/api/dataset/song.edit_choices`, {
      method: "POST",
      headers: { 
        "Cookie": ch,
        "Content-Type": "application/x-www-form-urlencoded"
      },
      body: new URLSearchParams({
        title: "Generic Dataset Song",
        type: "Entrada",
        csrf_token: csrfToken
      }).toString(),
    });

    if (postResp.status !== 201) {
      throw new Error(`Expected 201, got ${postResp.status}: ${await postResp.text()}`);
    }
    const postJson = await postResp.json();
    const key = postJson.id;
    if (!key) throw new Error("Missing key in create response");

    // 2. GET (Verify Create)
    const getResp1 = await fetch(`${BASE}/api/dataset/song.edit_choices`, { headers: { Cookie: ch } });
    const getJson1 = await getResp1.json();
    const row1 = getJson1.rows.find((r: any) => r.id === key);
    if (!row1 || row1.title !== "Generic Dataset Song") {
        throw new Error(`Row not found or value mismatch after create: ${JSON.stringify(getJson1)}`);
    }

    // 3. PUT (Update)
    const putResp = await fetch(`${BASE}/api/dataset/song.edit_choices/${key}`, {
      method: "PUT",
      headers: { 
        "Cookie": ch,
        "Content-Type": "application/x-www-form-urlencoded"
      },
      body: new URLSearchParams({
        title: "Updated Generic Song",
        data: "C G Am F",
        csrf_token: csrfToken
      }).toString(),
    });

    if (putResp.status !== 200) {
      throw new Error(`Expected 200, got ${putResp.status}: ${await putResp.text()}`);
    }

    // 4. GET (Verify Update)
    const getResp2 = await fetch(`${BASE}/api/dataset/song.edit_choices`, { headers: { Cookie: ch } });
    const getJson2 = await getResp2.json();
    const row2 = getJson2.rows.find((r: any) => r.id === key);
    if (!row2 || row2.title !== "Updated Generic Song") {
        throw new Error(`Row not found or value mismatch after update: ${JSON.stringify(getJson2)}`);
    }

    // 5. DELETE (Delete)
    const delResp = await fetch(`${BASE}/api/dataset/song.edit_choices/${key}`, {
      method: "DELETE",
      headers: { 
        "Cookie": ch,
        "Content-Type": "application/x-www-form-urlencoded"
      },
      body: new URLSearchParams({
        csrf_token: csrfToken
      }).toString(),
    });

    if (delResp.status !== 200) {
      throw new Error(`Expected 200, got ${delResp.status}: ${await delResp.text()}`);
    }

    // 6. GET (Verify Delete)
    const getResp3 = await fetch(`${BASE}/api/dataset/song.edit_choices`, { headers: { Cookie: ch } });
    const getJson3 = await getResp3.json();
    const row3 = getJson3.rows.find((r: any) => r.id === key);
    if (row3) {
        throw new Error(`Row still exists after delete: ${JSON.stringify(getJson3)}`);
    }

  } finally {
    await browser.close();
  }
});
