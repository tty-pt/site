/**
 * E2E test: POSIX group permissions for grp and private gig visibility
 *
 * Verifies:
 *   1. User A creates a group and adds User B as a member.
 *   2. User A creates a private gig associated with the group.
 *   3. User B (group member) CAN view the private gig (200 OK), but CANNOT edit it (403 Forbidden).
 *   4. User C (stranger) CANNOT view (403 Forbidden) and CANNOT edit (403 Forbidden).
 *   5. User A adds User C to the group -> User C can now view the private gig.
 *   6. User A removes User C from the group -> User C is once again forbidden (403).
 */

import { chromium } from "npm:playwright";
import { createAndLoginUser, getCsrfToken } from "./helpers/auth.ts";

const BASE = "http://localhost:8080";

Deno.test("grp filesystem permissions: group members can view private gigs but only owner can edit", async () => {
  const browser = await chromium.launch();
  const pageA = await browser.newPage();
  const pageB = await browser.newPage();
  const pageC = await browser.newPage();

  const ts = Date.now();
  const grpTitle = `Grp Perms Test ${ts}`;
  const gigTitle = `Private Gig Test ${ts}`;
  let grpId = "";
  let gigId = "";

  try {
    pageA.setDefaultNavigationTimeout(10000);
    pageA.setDefaultTimeout(10000);
    pageB.setDefaultNavigationTimeout(10000);
    pageB.setDefaultTimeout(10000);
    pageC.setDefaultNavigationTimeout(10000);
    pageC.setDefaultTimeout(10000);

    await pageA.route("**/styles.css", (route) => route.abort());
    await pageA.route("**/favicon.ico", (route) => route.abort());
    await pageB.route("**/styles.css", (route) => route.abort());
    await pageB.route("**/favicon.ico", (route) => route.abort());
    await pageC.route("**/styles.css", (route) => route.abort());
    await pageC.route("**/favicon.ico", (route) => route.abort());

    const GOTO = { waitUntil: "domcontentloaded" as const };

    // ── 1. User A: create group ──────────────────────────────────────────────
    await createAndLoginUser(pageA, BASE);
    await pageA.goto(`${BASE}/grp/add`, GOTO);
    await pageA.waitForSelector('input[name="title"]', { timeout: 5000 });
    await pageA.fill('input[name="title"]', grpTitle);
    await pageA.click('form[method="POST"] button[type="submit"]');

    await pageA.waitForURL(/\/grp\/[^/]+$/, { timeout: 5000 });
    grpId = pageA.url().split("/grp/")[1].replace(/\/$/, "");

    // ── 2. Register User B and User C ────────────────────────────────────────
    const userB = await createAndLoginUser(pageB, BASE);
    const cookiesB = await pageB.context().cookies();
    const cookieHeaderB = cookiesB.map((c) => `${c.name}=${c.value}`).join("; ");

    const userC = await createAndLoginUser(pageC, BASE);
    const cookiesC = await pageC.context().cookies();
    const cookieHeaderC = cookiesC.map((c) => `${c.name}=${c.value}`).join("; ");

    // ── 3. User A adds User B to the group ───────────────────────────────────
    const cookiesA = await pageA.context().cookies();
    const cookieHeaderA = cookiesA.map((c) => `${c.name}=${c.value}`).join("; ");
    const { token: csrfA, cookieHeader: chA } = await getCsrfToken(cookieHeaderA, BASE);

    const addMemberResp = await fetch(`${BASE}/api/grp/${grpId}/members`, {
      method: "POST",
      headers: {
        Cookie: chA,
        "Content-Type": "application/x-www-form-urlencoded",
      },
      body: `csrf_token=${csrfA}&action=add&member=${encodeURIComponent(userB.username)}`,
      redirect: "manual",
    });
    await addMemberResp.body?.cancel();

    // ── 4. User A creates gig linked to group ────────────────────────────────
    await pageA.goto(`${BASE}/gig/add?grp=${grpId}`, GOTO);
    await pageA.waitForSelector('input[name="title"]', { timeout: 5000 });
    await pageA.fill('input[name="title"]', gigTitle);
    await pageA.click('form[method="POST"] button[type="submit"]');
    await pageA.waitForURL(/\/gig\/[^/]+$/, { timeout: 5000 });
    gigId = pageA.url().split("/gig/")[1].replace(/\/$/, "");

    // Mark gig as private on disk (mode 0750 / private flag)
    try {
      await Deno.writeTextFile(`var/gig/${gigId}/private`, "1\n");
      await Deno.chmod(`var/gig/${gigId}`, 0o750);
    } catch (_) {
      // Ignore if running inside chroot
    }

    // ── 5. User B (member): CAN view gig (200 OK), CANNOT edit (403) ─────────
    const viewBResp = await fetch(`${BASE}/gig/${gigId}`, {
      headers: { Cookie: cookieHeaderB },
      redirect: "manual",
    });
    const viewBStatus = viewBResp.status;
    await viewBResp.body?.cancel();
    if (viewBStatus !== 200) {
      throw new Error(`Expected 200 for member User B viewing private gig, got ${viewBStatus}`);
    }

    const editBGet = await fetch(`${BASE}/gig/${gigId}/edit`, {
      headers: { Cookie: cookieHeaderB },
      redirect: "manual",
    });
    const editBStatus = editBGet.status;
    await editBGet.body?.cancel();
    if (editBStatus !== 403) {
      throw new Error(`Expected 403 for non-owner member User B GET edit, got ${editBStatus}`);
    }

    // ── 6. User C (stranger): CANNOT view (403), CANNOT edit (403) ───────────
    const viewCResp = await fetch(`${BASE}/gig/${gigId}`, {
      headers: { Cookie: cookieHeaderC },
      redirect: "manual",
    });
    const viewCStatus = viewCResp.status;
    await viewCResp.body?.cancel();
    if (viewCStatus !== 403) {
      throw new Error(`Expected 403 for stranger User C viewing private gig, got ${viewCStatus}`);
    }

    const editCGet = await fetch(`${BASE}/gig/${gigId}/edit`, {
      headers: { Cookie: cookieHeaderC },
      redirect: "manual",
    });
    const editCStatus = editCGet.status;
    await editCGet.body?.cancel();
    if (editCStatus !== 403) {
      throw new Error(`Expected 403 for stranger User C GET edit, got ${editCStatus}`);
    }

    // ── 7. User A adds User C to group ───────────────────────────────────────
    const addCResp = await fetch(`${BASE}/api/grp/${grpId}/members`, {
      method: "POST",
      headers: {
        Cookie: chA,
        "Content-Type": "application/x-www-form-urlencoded",
      },
      body: `csrf_token=${csrfA}&action=add&member=${encodeURIComponent(userC.username)}`,
      redirect: "manual",
    });
    await addCResp.body?.cancel();

    // ── 8. User C now CAN view the private gig ───────────────────────────────
    const viewC2Resp = await fetch(`${BASE}/gig/${gigId}`, {
      headers: { Cookie: cookieHeaderC },
      redirect: "manual",
    });
    const viewC2Status = viewC2Resp.status;
    await viewC2Resp.body?.cancel();
    if (viewC2Status !== 200) {
      throw new Error(`Expected 200 for newly-added member User C viewing private gig, got ${viewC2Status}`);
    }

    // ── 9. User A removes User C from group ──────────────────────────────────
    const delCResp = await fetch(`${BASE}/api/grp/${grpId}/members`, {
      method: "POST",
      headers: {
        Cookie: chA,
        "Content-Type": "application/x-www-form-urlencoded",
      },
      body: `csrf_token=${csrfA}&action=del&member=${encodeURIComponent(userC.username)}`,
      redirect: "manual",
    });
    await delCResp.body?.cancel();

    // ── 10. User C is once again forbidden (403) ─────────────────────────────
    const viewC3Resp = await fetch(`${BASE}/gig/${gigId}`, {
      headers: { Cookie: cookieHeaderC },
      redirect: "manual",
    });
    const viewC3Status = viewC3Resp.status;
    await viewC3Resp.body?.cancel();
    if (viewC3Status !== 403) {
      throw new Error(`Expected 403 for removed User C viewing private gig, got ${viewC3Status}`);
    }
  } finally {
    await browser.close();
  }
});
