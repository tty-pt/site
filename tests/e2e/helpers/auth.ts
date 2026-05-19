/**
 * Shared auth helpers for e2e tests.
 * All tests use a unique user per run to avoid session conflicts.
 */

export interface TestUser {
  username: string;
  password: string;
}

const LOG_FILE = "/tmp/site.log";

function skipConfirmRequired(): boolean {
  const env = Deno.env.get("AUTH_SKIP_CONFIRM");
  if (!env) return false;
  return !["0", "false", "FALSE", "no", "NO"].includes(env);
}

/**
 * Register a new user (POST to /auth/register).
 * Throws on non-303 response.
 */
export async function registerUser(
  base: string,
  user: TestUser,
): Promise<void> {
  const resp = await fetch(`${base}/auth/register`, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({
      username: user.username,
      password: user.password,
      password2: user.password,
      email: `${user.username}@example.com`,
    }).toString(),
    redirect: "manual",
  });
  if (resp.status !== 303) {
    const text = await resp.text();
    throw new Error(
      `registerUser: expected 303, got ${resp.status}\n${text.slice(0, 200)}`,
    );
  }
  await resp.body?.cancel();
}

/**
 * Confirm a user by polling the NDC log for the registration rcode.
 * Throws if rcode not found within 5s.
 */
export async function confirmUser(base: string, username: string): Promise<void> {
  const pattern = new RegExp(
    `ndc-auth: confirm: (/auth/confirm\\?u=${username}&r=[a-f0-9]+)`,
  );
  const deadline = Date.now() + 5000;

  while (Date.now() < deadline) {
    const stat = await Deno.stat(LOG_FILE).catch(() => null);
    if (!stat || stat.size === 0) {
      await new Promise((r) => setTimeout(r, 200));
      continue;
    }
    const tailSize = Math.min(stat.size, 65536);
    const file = await Deno.open(LOG_FILE, { read: true });
    await file.seek(-tailSize, Deno.SeekMode.End);
    const buf = new Uint8Array(tailSize);
    const n = await file.read(buf);
    file.close();
    const tail = new TextDecoder().decode(buf.subarray(0, n ?? tailSize));
    const lines = tail.split("\n").reverse();
    for (const line of lines) {
      const m = line.match(pattern);
      if (m) {
        const resp = await fetch(`${base}${m[1]}`, { redirect: "manual" });
        await resp.body?.cancel();
        return;
      }
    }
    await new Promise((r) => setTimeout(r, 200));
  }
  throw new Error(`confirmUser: rcode for ${username} not found in ${LOG_FILE}`);
}

/**
 * Full user lifecycle: register → confirm → login via browser.
 * When AUTH_SKIP_CONFIRM is enabled for tests, the confirm step is skipped.
 * Returns the TestUser object for convenience.
 */
export async function createAndLoginUser(
  page: import("npm:playwright").Page,
  base: string,
): Promise<TestUser> {
  const user: TestUser = {
    username: `e2e_${crypto.randomUUID().replace(/-/g, "").slice(0, 16)}`,
    password: `pw_${crypto.randomUUID().replace(/-/g, "").slice(0, 16)}`,
  };

  await registerUser(base, user);
  if (!skipConfirmRequired()) {
    await confirmUser(base, user.username);
  }
  await loginUser(page, base, user);

  return user;
}

/**
 * Login via browser page (fills login form, clicks submit, waits for redirect).
 */
export async function loginUser(
  page: import("npm:playwright").Page,
  base: string,
  user: TestUser,
): Promise<void> {
  const GOTO = { waitUntil: "domcontentloaded" as const };
  await page.goto(`${base}/auth/login`, GOTO);
  await page.fill('input[name="username"]', user.username);
  await page.fill('input[name="password"]', user.password);
  await Promise.all([
    page.waitForURL(`${base}/`, { waitUntil: "domcontentloaded", timeout: 5000 }),
    page.click('button[type="submit"]'),
  ]);
}

/**
 * Logout via browser (navigates to /logout).
 */
export async function logoutUser(
  page: import("npm:playwright").Page,
  base: string,
): Promise<void> {
  await Promise.all([
    page.waitForURL(`${base}/`, { waitUntil: "domcontentloaded", timeout: 5000 }),
    page.goto(`${base}/auth/logout`, { waitUntil: "domcontentloaded" }),
  ]);
}

/**
 * Fetch a CSRF token from GET /api/csrf using the given cookie header.
 * Returns { token, cookieHeader } where cookieHeader has the csrf_token cookie
 * appended so that subsequent fetch calls send the matching cookie.
 */
export async function getCsrfToken(
  cookieHeader: string,
  base: string,
): Promise<{ token: string; cookieHeader: string }> {
  const resp = await fetch(`${base}/api/csrf`, {
    headers: { Cookie: cookieHeader },
  });
  const token = (await resp.text()).trim();
  if (!token || token.length < 16) {
    throw new Error(`getCsrfToken: unexpected response "${token}"`);
  }
  // Extract csrf_token value from Set-Cookie header and append to cookie string.
  const setCookie = resp.headers.get("set-cookie") ?? "";
  const match = setCookie.match(/csrf_token=([^;]+)/);
  const csrfCookieVal = match ? match[1] : token;
  // Remove any existing csrf_token entry then append the fresh one.
  const base64 = cookieHeader
    .split("; ")
    .filter((p) => !p.startsWith("csrf_token="))
    .join("; ");
  const updated = base64 ? `${base64}; csrf_token=${csrfCookieVal}` : `csrf_token=${csrfCookieVal}`;
  return { token, cookieHeader: updated };
}

/**
 * Wait for an element's text content to include an expected substring.
 */
export async function waitForText(
  page: import("npm:playwright").Page,
  selector: string,
  expected: string,
  timeout = 5000,
): Promise<void> {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    const text = await page.textContent(selector);
    if (text?.includes(expected)) return;
    await new Promise((r) => setTimeout(r, 100));
  }
  const actual = await page.textContent(selector);
  throw new Error(
    `waitForText: "${expected}" not found in "${selector}" within ${timeout}ms.\nActual: "${actual}"`,
  );
}
