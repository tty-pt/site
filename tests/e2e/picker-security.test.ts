import { chromium, type Page } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";
const GOTO = { waitUntil: "domcontentloaded" as const };

function cookieHeader(page: Page): Promise<string> {
  return page.context().cookies().then((cookies) =>
    cookies.map((cookie) => `${cookie.name}=${cookie.value}`).join("; ")
  );
}

function replaceCsrfCookie(header: string, token: string): string {
  const cookies = header.split("; ").filter((cookie) =>
    !cookie.startsWith("csrf_token=")
  );
  cookies.push(`csrf_token=${token}`);
  return cookies.join("; ");
}

async function createItem(
  page: Page,
  module: "song" | "grp" | "gig",
  title: string,
) {
  await page.goto(`${BASE}/${module}/add`, GOTO);
  await page.fill('input[name="title"]', title);
  await page.click('form[method="POST"] button[type="submit"]');
  await page.waitForURL(new RegExp(`/${module}/[^/]+$`));
  return page.url().split(`/${module}/`)[1].replace(/\/$/, "");
}

async function postPicker(
  module: "grp" | "gig",
  id: string,
  songId: string,
  cookies: string,
  csrfToken?: string,
): Promise<number> {
  const body = new URLSearchParams({ song_id: songId, format: "any" });
  if (csrfToken !== undefined) body.set("csrf_token", csrfToken);

  const response = await fetch(`${BASE}/api/${module}/${id}/songs`, {
    method: "POST",
    headers: {
      Cookie: cookies,
      "Content-Type": "application/x-www-form-urlencoded",
    },
    body: body.toString(),
    redirect: "manual",
  });
  const status = response.status;
  await response.body?.cancel();
  return status;
}

async function orderedRows(page: Page, module: "grp" | "gig", id: string) {
  await page.goto(`${BASE}/${module}/${id}`, GOTO);
  const selector = module === "grp"
    ? `a[href^="/grp/${id}/song/"]`
    : "[data-gig-item]";
  return await page.locator(selector).allTextContents().then((rows) =>
    rows.map((row) => row.replace(/\s+/g, " ").trim())
  );
}

function assertStatus(status: number, label: string) {
  if (status !== 403) throw new Error(`${label}: expected 403, got ${status}`);
}

function assertRows(actual: string[], expected: string[], label: string) {
  if (JSON.stringify(actual) !== JSON.stringify(expected)) {
    throw new Error(
      `${label}: ordered UI changed\nExpected: ${
        JSON.stringify(expected)
      }\nActual: ${JSON.stringify(actual)}`,
    );
  }
}

Deno.test({
  name:
    "grp and gig pickers reject non-owners and invalid CSRF without mutation",
  sanitizeResources: false,
  sanitizeOps: false,
}, async () => {
  const browser = await chromium.launch();
  const owner = await browser.newPage();
  const attacker = await browser.newPage();

  try {
    owner.setDefaultTimeout(10000);
    owner.setDefaultNavigationTimeout(10000);
    attacker.setDefaultTimeout(10000);

    await createAndLoginUser(owner, BASE);
    const suffix = crypto.randomUUID().replaceAll("-", "").slice(0, 12);
    const songId = await createItem(
      owner,
      "song",
      `Picker Security Song ${suffix}`,
    );
    const grpId = await createItem(
      owner,
      "grp",
      `Picker Security Grp ${suffix}`,
    );
    const gigId = await createItem(
      owner,
      "gig",
      `Picker Security Gig ${suffix}`,
    );

    const ownerCookies = await cookieHeader(owner);
    const ownerCsrf = await getCsrfToken(ownerCookies, BASE);

    for (const [module, id] of [["grp", grpId], ["gig", gigId]] as const) {
      const status = await postPicker(
        module,
        id,
        songId,
        ownerCsrf.cookieHeader,
        ownerCsrf.token,
      );
      if (status !== 303) {
        throw new Error(`${module} baseline add: expected 303, got ${status}`);
      }
    }

    const baseline = {
      grp: await orderedRows(owner, "grp", grpId),
      gig: await orderedRows(owner, "gig", gigId),
    };
    if (baseline.grp.length !== 1 || baseline.gig.length !== 1) {
      throw new Error(
        `Expected one baseline row in each picker: ${JSON.stringify(baseline)}`,
      );
    }

    await createAndLoginUser(attacker, BASE);
    const attackerCsrf = await getCsrfToken(await cookieHeader(attacker), BASE);
    for (const [module, id] of [["grp", grpId], ["gig", gigId]] as const) {
      assertStatus(
        await postPicker(
          module,
          id,
          songId,
          attackerCsrf.cookieHeader,
          attackerCsrf.token,
        ),
        `attacker ${module} picker POST`,
      );
    }
    assertRows(
      await orderedRows(owner, "grp", grpId),
      baseline.grp,
      "attacker grp POST",
    );
    assertRows(
      await orderedRows(owner, "gig", gigId),
      baseline.gig,
      "attacker gig POST",
    );

    const staleCookies = replaceCsrfCookie(
      ownerCsrf.cookieHeader,
      crypto.randomUUID().replaceAll("-", ""),
    );
    const cases = [
      { name: "missing", cookies: ownerCsrf.cookieHeader, token: undefined },
      {
        name: "incorrect",
        cookies: ownerCsrf.cookieHeader,
        token: "0".repeat(32),
      },
      { name: "stale", cookies: staleCookies, token: ownerCsrf.token },
    ] as const;

    for (const csrfCase of cases) {
      for (const [module, id] of [["grp", grpId], ["gig", gigId]] as const) {
        assertStatus(
          await postPicker(
            module,
            id,
            songId,
            csrfCase.cookies,
            csrfCase.token,
          ),
          `owner ${module} POST with ${csrfCase.name} CSRF`,
        );
      }
      assertRows(
        await orderedRows(owner, "grp", grpId),
        baseline.grp,
        `owner grp POST with ${csrfCase.name} CSRF`,
      );
      assertRows(
        await orderedRows(owner, "gig", gigId),
        baseline.gig,
        `owner gig POST with ${csrfCase.name} CSRF`,
      );
    }
  } finally {
    await browser.close();
  }
});
