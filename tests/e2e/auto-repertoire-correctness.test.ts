import { chromium, type Locator, type Page } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const GOTO = { waitUntil: "domcontentloaded" as const };

const A = {
  id: "a_alegria_esta_no_coracao",
  title: "A alegria está no coração",
};
const B = {
  id: "abencoai_a_nossa_oferta",
  title: "Abençoai a nossa oferta",
};
const C = { id: "fomos_resgatados", title: "Fomos resgatados" };
const D = { id: "a_bondade_do_senhor", title: "A bondade do Senhor" };

type Song = typeof A;
type GigRow = { song: Song; key: number };

function assertEquals<T>(actual: T, expected: T, message: string): void {
  if (actual !== expected) {
    throw new Error(`${message}: expected ${expected}, got ${actual}`);
  }
}

function assertArrayEquals(
  actual: string[],
  expected: string[],
  message: string,
): void {
  if (
    actual.length !== expected.length ||
    actual.some((value, i) => value !== expected[i])
  ) {
    throw new Error(
      `${message}: expected [${expected.join(", ")}], got [${
        actual.join(", ")
      }]`,
    );
  }
}

function repertoireRow(page: Page, grpId: string, songId: string): Locator {
  return page.locator(`a[href="/grp/${grpId}/song/${songId}"]`).locator(
    "xpath=../..",
  );
}

async function repertoireOrder(page: Page, grpId: string): Promise<string[]> {
  const links = page.locator(`a[href^="/grp/${grpId}/song/"]`);
  const ids: string[] = [];
  for (let i = 0; i < await links.count(); i++) {
    const href = await links.nth(i).getAttribute("href");
    if (!href) throw new Error(`repertoire link ${i} has no href`);
    ids.push(href.slice(href.lastIndexOf("/") + 1));
  }
  return ids;
}

async function assertRowKey(
  page: Page,
  grpId: string,
  songId: string,
  expected: string,
): Promise<void> {
  const row = repertoireRow(page, grpId, songId);
  assertEquals(await row.count(), 1, `repertoire row ${songId} count`);
  const select = row.locator(
    `form[action="/api/grp/${grpId}/song/${songId}/key"] select[name="key"]`,
  );
  assertEquals(await select.inputValue(), expected, `${songId} transpose`);
}

async function assertPinned(
  page: Page,
  grpId: string,
  songId: string,
  expected: boolean,
): Promise<void> {
  const row = repertoireRow(page, grpId, songId);
  assertEquals(await row.count(), 1, `repertoire row ${songId} count`);
  const label = await row.locator("span.text-xs.text-muted").textContent() ??
    "";
  assertEquals(label.includes("pinned"), expected, `${songId} pinned marker`);
  const remove = row.locator(
    `form[action="/api/grp/${grpId}/song/${songId}/remove"] button`,
  );
  assertEquals(
    await remove.count(),
    expected ? 1 : 0,
    `${songId} Remove control count`,
  );
}

Deno.test({
  name:
    "auto-repertoire correctness: majority, pinning, order, and stale removal",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();

  try {
    page.setDefaultNavigationTimeout(15000);
    page.setDefaultTimeout(15000);
    await page.route("**/styles.css", (route) => route.abort());

    await createAndLoginUser(page, BASE);
    const cookies = await page.context().cookies();
    const authCookies = cookies.map((cookie) =>
      `${cookie.name}=${cookie.value}`
    ).join("; ");

    async function post(
      path: string,
      body: URLSearchParams | FormData,
    ): Promise<void> {
      const { token, cookieHeader } = await getCsrfToken(authCookies, BASE);
      body.append("csrf_token", token);
      const headers: HeadersInit = { Cookie: cookieHeader };
      if (body instanceof URLSearchParams) {
        headers["Content-Type"] = "application/x-www-form-urlencoded";
      }
      const response = await fetch(`${BASE}${path}`, {
        method: "POST",
        body,
        headers,
        redirect: "manual",
      });
      if (response.status >= 400) {
        const text = await response.text();
        throw new Error(
          `POST ${path} failed ${response.status}: ${text.slice(0, 200)}`,
        );
      }
      await response.body?.cancel();
    }

    async function createGig(grpId: string, title: string): Promise<string> {
      await page.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
      await page.fill('input[name="title"]', title);
      await page.click('form[method="POST"] button[type="submit"]');
      await page.waitForURL(/\/gig\/[^/]+$/);
      return page.url().split("/gig/")[1].replace(/\/$/, "");
    }

    async function editGig(
      gigId: string,
      title: string,
      grpId: string,
      rows: GigRow[],
    ): Promise<void> {
      const body = new FormData();
      body.append("title", title);
      body.append("grp", grpId);
      body.append("amount", String(rows.length));
      rows.forEach(({ song, key }, i) => {
        body.append(`song_${i}`, `${song.title} [${song.id}]`);
        body.append(`key_${i}`, String(key));
        body.append(`orig_${i}`, "0");
        body.append(`fmt_${i}`, "any");
      });
      body.append("action", "save");
      await post(`/gig/${gigId}/edit`, body);
    }

    const stamp = `${Date.now()}-${crypto.randomUUID().slice(0, 8)}`;
    const grpTitle = `Auto repertoire correctness ${stamp}`;
    await page.goto(`${BASE}/grp/add`, GOTO);
    await page.fill('input[name="title"]', grpTitle);
    await page.click('form[method="POST"] button[type="submit"]');
    await page.waitForURL(/\/grp\/[^/]+$/);
    const grpId = page.url().split("/grp/")[1].replace(/\/$/, "");

    const gig1Title = `Auto repertoire first ${stamp}`;
    const gig2Title = `Auto repertoire second ${stamp}`;
    const gig1 = await createGig(grpId, gig1Title);
    const gig2 = await createGig(grpId, gig2Title);
    await editGig(gig1, gig1Title, grpId, [
      { song: A, key: 2 },
      { song: B, key: 0 },
      { song: D, key: 0 },
    ]);
    await editGig(gig2, gig2Title, grpId, [{ song: A, key: 0 }]);

    await page.goto(`${BASE}/grp/${grpId}`, GOTO);
    await assertRowKey(page, grpId, A.id, "2");
    await assertRowKey(page, grpId, B.id, "0");
    await assertPinned(page, grpId, A.id, false);
    await assertPinned(page, grpId, B.id, false);
    assertArrayEquals(
      await repertoireOrder(page, grpId),
      [A.id, B.id, D.id],
      "initial first-seen derived order",
    );

    const gig3Title = `Auto repertoire third ${stamp}`;
    const gig3 = await createGig(grpId, gig3Title);
    await editGig(gig3, gig3Title, grpId, [{ song: A, key: 3 }]);
    await editGig(gig1, gig1Title, grpId, [
      { song: A, key: 3 },
      { song: B, key: 0 },
      { song: D, key: 0 },
    ]);
    await page.goto(`${BASE}/grp/${grpId}`, GOTO);
    await assertRowKey(page, grpId, A.id, "3");

    await post(
      `/api/grp/${grpId}/song/${A.id}/key`,
      new URLSearchParams({ key: "-1" }),
    );
    await post(
      `/api/grp/${grpId}/songs`,
      new URLSearchParams({ song_id: C.id, format: "any" }),
    );
    await page.goto(`${BASE}/grp/${grpId}`, GOTO);
    await assertPinned(page, grpId, A.id, true);
    await assertPinned(page, grpId, C.id, true);
    const pinnedLabel = await repertoireRow(page, grpId, A.id)
      .locator("span.text-xs.text-muted").textContent() ?? "";
    if (!pinnedLabel.includes("Key: Ab")) {
      throw new Error(`expected A at pinned key -1 (Ab), got "${pinnedLabel}"`);
    }
    assertArrayEquals(
      await repertoireOrder(page, grpId),
      [A.id, C.id, B.id, D.id],
      "pinned-first order",
    );

    await editGig(gig2, gig2Title, grpId, [{ song: A, key: 5 }]);
    await page.goto(`${BASE}/grp/${grpId}`, GOTO);
    const labelAfterGigChange = await repertoireRow(page, grpId, A.id)
      .locator("span.text-xs.text-muted").textContent() ?? "";
    assertEquals(
      labelAfterGigChange,
      pinnedLabel,
      "pinned key after gig key change",
    );
    assertArrayEquals(
      await repertoireOrder(page, grpId),
      [A.id, C.id, B.id, D.id],
      "relative order after rebuild",
    );

    await post(
      `/api/grp/${grpId}/song/${A.id}/remove`,
      new URLSearchParams(),
    );
    await page.goto(`${BASE}/grp/${grpId}`, GOTO);
    await assertPinned(page, grpId, A.id, false);
    await assertRowKey(page, grpId, A.id, "3");
    assertArrayEquals(
      await repertoireOrder(page, grpId),
      [C.id, A.id, B.id, D.id],
      "removed pin returns behind pinned rows as derived",
    );

    await editGig(gig1, gig1Title, grpId, [
      { song: A, key: 3 },
      { song: D, key: 0 },
    ]);
    await page.goto(`${BASE}/grp/${grpId}`, GOTO);
    assertEquals(
      await repertoireRow(page, grpId, B.id).count(),
      0,
      "derived song after its final gig occurrence is removed",
    );
    assertArrayEquals(
      await repertoireOrder(page, grpId),
      [C.id, A.id, D.id],
      "final pinned and derived order",
    );

    const repertoirePath = `var/grp/${grpId}/data.txt`;
    const beforeContents = await Deno.readTextFile(repertoirePath);
    const beforeMtime = (await Deno.stat(repertoirePath)).mtime?.getTime();
    await new Promise((resolve) => setTimeout(resolve, 1100));
    await page.goto(`${BASE}/grp/${grpId}`, GOTO);
    assertEquals(
      await Deno.readTextFile(repertoirePath),
      beforeContents,
      "no-op detail rebuild contents",
    );
    assertEquals(
      (await Deno.stat(repertoirePath)).mtime?.getTime(),
      beforeMtime,
      "no-op detail rebuild mtime",
    );
  } finally {
    await browser.close();
  }
});
