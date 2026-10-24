import { chromium } from "npm:playwright";
import { Buffer } from "node:buffer";
import { createAndLoginUser } from "./helpers/auth.ts";

const BASE = Deno.env.get("BASE_URL") || "http://localhost:8080";

async function waitForTerminal(page: any, substr: string, timeoutMs = 8000) {
  await page.waitForFunction(
    (s: string) => {
      const rows = document.querySelectorAll("#term .xterm-rows > div");
      return Array.from(rows).map((r) => r.textContent || "").join("\n").includes(s);
    },
    substr,
    { timeout: timeoutMs }
  );
}

Deno.test("nd e2e: full sequential all-controls interaction and visual verification", async () => {
  const browser = await chromium.launch({
    args: ["--no-sandbox", "--disable-setuid-sandbox", "--disable-dev-shm-usage"],
  });
  const context = await browser.newContext();
  const page = await context.newPage();

  try {
    page.on("console", (msg) => console.log("PAGE CONSOLE:", msg.type(), msg.text()));
    page.on("pageerror", (err) => console.log("PAGE ERROR:", err.message, "\nSTACK:", err.stack));

    // The player in this run holds no region, so tenant game modules cannot
    // answer it. Proxy the real game socket and play the *server side* of the
    // spell/vitals protocol from the test, with byte-exact BCP frames, so the
    // full client pipeline (decode -> state -> DOM -> click -> command -> reply
    // -> gauge) is exercised in a real browser.
    const sentToServer: string[] = [];
    const sock: { cur: { send: (m: string | Buffer) => void } | null } = { cur: null };
    await page.routeWebSocket(/\/nd(\?.*)?$/, (ws: any) => {
      const server = ws.connectToServer();
      sock.cur = ws;
      ws.onMessage((m: string | Uint8Array) => {
        sentToServer.push(typeof m === "string" ? m : new TextDecoder().decode(m));
        server.send(m);
      });
      server.onMessage((m: string | Uint8Array) => ws.send(m));
    });
    const bcp = (opcode: number, payload: number[]) =>
      Buffer.from([0x23, 0x62, opcode, ...payload]);
    const u16 = (n: number) => [n & 0xff, (n >> 8) & 0xff];
    const u32 = (n: number) => [n & 0xff, (n >> 8) & 0xff, (n >> 16) & 0xff, (n >>> 24) & 0xff];
    const spellWire = (slot: number, skel: number, cost: number, element: number, name: string) => {
      const nm = new Array(24).fill(0);
      for (let i = 0; i < Math.min(23, name.length); i++) nm[i] = name.charCodeAt(i);
      return [slot, ...u32(skel), ...u16(cost), element, 0, ...nm];
    };

    // 0. Authenticate test player via register + login
    await createAndLoginUser(page, BASE);

    // 1. Navigate to NeverDark web portal
    await page.goto(`${BASE}/nd`, { waitUntil: "domcontentloaded" });

    // Verify SSR State
    const stateEl = await page.waitForSelector("#bud-state", { state: "attached", timeout: 5000 });
    if (!stateEl) throw new Error("Missing script#bud-state in SSR output");
    const state = JSON.parse((await stateEl.textContent()) || "{}");
    if (!Array.isArray(state.gauges)) {
      throw new Error(`SSR state missing generic gauges array: ${JSON.stringify(state)}`);
    }

    // Wait for WASM Hydration
    await page.waitForSelector("body[data-wasm-loaded]", { timeout: 10000 });
    await page.waitForSelector("#bud-root", { timeout: 5000 });
    await page.waitForSelector("#term .xterm", { timeout: 8000 });
    // Gauges are module-contributed; after hydration the vitals gauge must exist
    await page.waitForSelector('[data-gauge="hp"]', { state: "attached", timeout: 8000 });

    // =========================================================================
    // STEP 1: Site Chrome Navigation Drawer & Top Bar Proportions
    // =========================================================================
    const navBarHeight = await page.$eval(".nav-bar", (el) => el.getBoundingClientRect().height);
    if (navBarHeight > 64) {
      throw new Error(`Top bar is taller than standard site height: ${navBarHeight}px > 64px`);
    }

    const toggle = await page.waitForSelector('[data-menu-toggle="1"]', { timeout: 5000 });
    if (!toggle) throw new Error("Missing site chrome menu toggle button");
    const toggleIcon = await toggle.textContent();
    if (!toggleIcon?.includes("🕹️")) {
      throw new Error(`Expected menu toggle icon '🕹️', got '${toggleIcon}'`);
    }

    const toggleGeom = await toggle.evaluate((el) => {
      const rect = el.getBoundingClientRect();
      return { w: Math.round(rect.width), h: Math.round(rect.height) };
    });
    if (toggleGeom.w > 48 || toggleGeom.h > 48) {
      throw new Error(`Menu toggle is wider/taller than usual: ${toggleGeom.w}x${toggleGeom.h}px`);
    }

    await toggle.click();
    await page.waitForFunction(() => {
      const cb = document.querySelector("#menu-functions") as HTMLInputElement;
      return cb && cb.checked;
    });

    const funcsStyle = await page.$eval(".functions", (el) => {
      const cs = window.getComputedStyle(el);
      const rect = el.getBoundingClientRect();
      return {
        display: cs.display,
        visibility: cs.visibility,
        opacity: cs.opacity,
        w: rect.width,
        h: rect.height,
        x: rect.x,
      };
    });
    if (funcsStyle.display === "none" || funcsStyle.visibility === "hidden" || funcsStyle.w === 0 || funcsStyle.h === 0) {
      throw new Error(`Navigation drawer .functions is not visible: ${JSON.stringify(funcsStyle)}`);
    }

    const navLinks = await page.$$eval(".functions a", (els) => els.map((e) => e.getAttribute("href")));
    // User is authenticated, so drawer displays standard site auth menu: Me and Logout
    if (!navLinks.some((href) => href && href.includes("/auth/logout"))) {
      throw new Error(`Navigation drawer missing logout link: found ${JSON.stringify(navLinks)}`);
    }

    // Close menu drawer via backdrop overlay
    const overlay = await page.waitForSelector("label.menu-overlay", { timeout: 3000 });
    await overlay.click();
    await page.waitForFunction(() => {
      const cb = document.querySelector("#menu-functions") as HTMLInputElement;
      return cb && !cb.checked;
    });

    // =========================================================================
    // STEP 2: Direction Keypad Sequential Execution (All 6 Directions)
    // =========================================================================
    const dirs = [
      { sel: 'button[title="North (k)"]', cmd: "k" },
      { sel: 'button[title="Northeast (K)"]', cmd: "K" },
      { sel: 'button[title="West (h)"]', cmd: "h" },
      { sel: 'button[title="East (l)"]', cmd: "l" },
      { sel: 'button[title="South (j)"]', cmd: "j" },
      { sel: 'button[title="Southeast (J)"]', cmd: "J" },
    ];
    for (const d of dirs) {
      const btn = await page.waitForSelector(d.sel, { timeout: 3000 });
      if (!btn) throw new Error(`Missing direction button: ${d.sel}`);

      // Verify circular 32x32px geometry
      const geom = await btn.evaluate((el) => {
        const cs = window.getComputedStyle(el);
        const rect = el.getBoundingClientRect();
        return { br: cs.borderRadius, w: Math.round(rect.width), h: Math.round(rect.height) };
      });
      if (geom.br !== "9999px" && !geom.br.includes("50%")) {
        throw new Error(`Direction button '${d.sel}' is not circular (borderRadius='${geom.br}')`);
      }
      if (geom.w !== 32 || geom.h !== 32) {
        throw new Error(`Direction button '${d.sel}' dimensions are not 32x32px: ${geom.w}x${geom.h}`);
      }

      await btn.click();
      await page.waitForTimeout(150);
    }

    // =========================================================================
    // STEP 3: Live Room Map ANSI Escape Hygiene & Biome/Tree Rendering
    // =========================================================================
    const mapEl = await page.waitForSelector("nd-map#room-map", { timeout: 5000 });
    if (!mapEl) throw new Error("Missing nd-map#room-map element");

    const mapText = await mapEl.evaluate((el) => el.textContent || "");
    if (!mapText || mapText.length < 20) {
      throw new Error(`Room map content is too short: '${mapText}' (len=${mapText.length})`);
    }

    // Assert zero raw ANSI escape sequences leaked into the map DOM
    if (mapText.includes("\x1b") || mapText.includes("[40m") || mapText.includes("[0m") || mapText.includes("[1m")) {
      throw new Error(`Room map contains unparsed ANSI escape sequences:\n${mapText}`);
    }

    const mapHtml = await mapEl.evaluate((el) => el.innerHTML);
    console.log(`Live room map HTML length: ${mapHtml.length}, text length: ${mapText.length}`);

    // =========================================================================
    // STEP 4: Interactive Terminal Execution & Hygiene
    // =========================================================================
    const ta = await page.waitForSelector("#term textarea", { state: "attached", timeout: 5000 });
    if (!ta) throw new Error("Terminal input textarea not found");
    await ta.focus();

    // Verify terminal focus border (#9589c5 == rgb(149, 137, 197))
    const termBorder = await page.evaluate(() => {
      const term = document.querySelector("#term");
      return term ? window.getComputedStyle(term).borderColor : "";
    });
    if (!termBorder.includes("149, 137, 197")) {
      throw new Error(`Expected #term focus border rgb(149, 137, 197), got '${termBorder}'`);
    }

    // Execute 'say HelloNeverDark' and assert echo
    await page.evaluate(() => {
      const ta = document.querySelector("#term textarea") as HTMLTextAreaElement;
      if (ta) ta.focus();
    });
    await page.waitForTimeout(500);
    await page.keyboard.type("say HelloCitadel\n");
    await page.waitForTimeout(1000);

    await waitForTerminal(page, "You say: HelloCitadel", 6000);

    // Execute 'look' and assert room description
    await page.keyboard.type("look\n");
    await waitForTerminal(page, "Citadel", 6000);

    // Assert zero binary replacement characters or protocol leaks
    const fullTerminalText = await page.evaluate(() => {
      const rows = document.querySelectorAll("#term .xterm-rows > div");
      return Array.from(rows).map((r) => r.textContent || "").join("\n");
    });
    if (fullTerminalText.includes("\ufffd") || fullTerminalText.includes("#b")) {
      throw new Error(`Terminal contains binary data or protocol leakage: ${fullTerminalText}`);
    }

    // =========================================================================
    // STEP 5: Primary Action Dock Sequential Execution
    // =========================================================================
    // Assert #g-actions is fully visible within the browser viewport
    const gActionsPos = await page.evaluate(() => {
      const dock = document.querySelector("#g-actions");
      if (!dock) return null;
      const rect = dock.getBoundingClientRect();
      return { bottom: rect.bottom, y: rect.y, height: rect.height, windowH: window.innerHeight };
    });
    if (!gActionsPos || gActionsPos.bottom > gActionsPos.windowH) {
      throw new Error(`Action dock (#g-actions) is pushed below the screen: bottom=${gActionsPos?.bottom} > windowH=${gActionsPos?.windowH}`);
    }

    // Assert Equipment button (👕) and Character Sheet button (📋) exist and are visible
    const equipRect = await page.$eval('button[title="Equipment"]', (el) => {
      const rect = el.getBoundingClientRect();
      return { w: rect.width, h: rect.height, bottom: rect.bottom };
    });
    const windowH = await page.evaluate(() => window.innerHeight);
    if (equipRect.w === 0 || equipRect.h === 0 || equipRect.bottom > windowH) {
      throw new Error(`Equipment button is not visible on screen: ${JSON.stringify(equipRect)}`);
    }

    const statsRect = await page.$eval('button[title="Character Sheet"]', (el) => {
      const rect = el.getBoundingClientRect();
      return { w: rect.width, h: rect.height, bottom: rect.bottom };
    });
    if (statsRect.w === 0 || statsRect.h === 0 || statsRect.bottom > windowH) {
      throw new Error(`Character sheet button is not visible on screen: ${JSON.stringify(statsRect)}`);
    }

    // 5a. Inventory (📦)
    const invBtn = await page.waitForSelector('button[title="Inventory"]', { timeout: 3000 });
    await invBtn.click();
    await page.waitForTimeout(500);

    // 5b. Look (👁)
    const lookBtn = await page.waitForSelector('button[title="Look"]', { timeout: 3000 });
    await lookBtn.click();
    await waitForTerminal(page, "Citadel", 6000);

    // 5c. Spellbook Modal (📜)
    const spellBtn = await page.waitForSelector('button[title="Spellbook"]', { timeout: 3000 });
    await spellBtn.click();
    await page.waitForSelector("#spell-modal:not(.hidden)", { timeout: 3000 });

    // Assert #spell-modal has .modal class
    const spellModalHasModalClass = await page.$eval("#spell-modal", (el) => el.classList.contains("modal"));
    if (!spellModalHasModalClass) {
      throw new Error("Spellbook modal (#spell-modal) is missing .modal class");
    }

    // Assert inner #spell card exists, has purple border and #2c2c2c background, and no rounded corners
    const spellCardStyle = await page.$eval("#spell", (el) => {
      const cs = window.getComputedStyle(el);
      return {
        bc: cs.borderColor,
        bg: cs.backgroundColor,
        br: cs.borderRadius,
      };
    });
    if (!spellCardStyle.bc.includes("149, 137, 197") && !spellCardStyle.bc.includes("#9589c5")) {
      throw new Error(`Expected purple border #9589c5 on #spell, got ${spellCardStyle.bc}`);
    }
    if (spellCardStyle.br !== "0px" && spellCardStyle.br !== "0") {
      throw new Error(`Expected 0px border radius on #spell, got ${spellCardStyle.br}`);
    }

    // Server side of the protocol (played by the test): vitals + spell list
    if (!sock.cur) throw new Error("game socket was never proxied");
    sock.cur!.send(bcp(13, [...u16(50), ...u16(50)])); // ND_BCP_MP 50/50
    sock.cur!.send(bcp(16, [
      3, // count
      ...spellWire(0, 11, 3, 2, "Fireball"),
      ...spellWire(1, 12, 15, 4, "Freeze"),
      ...spellWire(2, 13, 99, 16, "Stone Skin"), // unaffordable
    ]));

    try {
      await page.waitForFunction(
        () => document.querySelectorAll("#spell-modal [data-spell-slot]:not(.hidden)").length === 3 &&
          (document.querySelector("#spell-modal")?.textContent || "").includes("99 MP"),
        null,
        { timeout: 6000 }
      );
    } catch (e) {
      console.log("SPELL MODAL HTML:", await page.$eval("#spell-modal", (el) => el.outerHTML));
      throw e;
    }
    const spellRows = await page.$$eval("#spell-modal [data-spell-slot]:not(.hidden)", (els) =>
      els.map((e) => ({
        slot: e.getAttribute("data-spell-slot"),
        cmd: e.getAttribute("data-cmd") || null,
        disabled: e.getAttribute("data-disabled") || null,
        text: (e.textContent || "").trim(),
      }))
    );
    if (spellRows.length !== 3) throw new Error(`Expected 3 spell rows, got ${JSON.stringify(spellRows)}`);
    if (!spellRows[0].text.includes("Fireball") || !spellRows[0].text.includes("3 MP")) {
      throw new Error(`Row 0 does not show the pushed spell: ${JSON.stringify(spellRows[0])}`);
    }
    if (spellRows[0].cmd !== "c0" || spellRows[1].cmd !== "c1") {
      throw new Error(`Affordable rows must carry cast commands: ${JSON.stringify(spellRows)}`);
    }
    if (spellRows[2].cmd !== null || spellRows[2].disabled !== "1") {
      throw new Error(`Unaffordable spell must be disabled with no command: ${JSON.stringify(spellRows[2])}`);
    }
    if (!(await page.$eval("#spell-empty", (el) => el.classList.contains("hidden")))) {
      throw new Error("Grimoire shows its empty-state line although spells were pushed");
    }

    // Clicking a disabled row sends nothing
    sentToServer.length = 0;
    await page.click("#spell-modal [data-spell-slot='2']");
    await page.waitForTimeout(300);
    if (sentToServer.some((m) => /^c\d/.test(m))) throw new Error("Disabled spell row sent a cast command");

    // Casting an affordable spell sends `c0`; the server answers with the new MP
    const readMp = async () =>
      await page.$eval('[data-gauge="mp"]', (el) => (el.textContent || "").trim());
    if ((await readMp()) !== "50/50") throw new Error(`MP gauge should read 50/50, got ${await readMp()}`);
    await page.click("#spell-modal [data-spell-slot='0']");
    await page.waitForFunction(() => true);
    const deadline = Date.now() + 4000;
    while (!sentToServer.some((m) => m.startsWith("c0")) && Date.now() < deadline) {
      await page.waitForTimeout(50);
    }
    if (!sentToServer.some((m) => m.startsWith("c0"))) {
      throw new Error(`Clicking the row did not send 'c0': ${JSON.stringify(sentToServer)}`);
    }
    sock.cur!.send(bcp(13, [...u16(47), ...u16(50)]));   // MP bar 47/50
    sock.cur!.send(bcp(17, [0, 2, ...u32(0)]));           // cast ack: slot 0, fire
    await page.waitForFunction(
      () => (document.querySelector('[data-gauge="mp"]')?.textContent || "").trim() === "47/50",
      null,
      { timeout: 4000 }
    );
    // Spell list refresh after casting (costs may change): Freeze now unaffordable at MP 10
    sock.cur!.send(bcp(13, [...u16(10), ...u16(50)]));
    sock.cur!.send(bcp(16, [1, ...spellWire(0, 11, 3, 2, "Fireball")]));
    await page.waitForFunction(
      () => document.querySelectorAll("#spell-modal [data-spell-slot]:not(.hidden)").length === 1,
      null,
      { timeout: 4000 }
    );
    console.log("MP gauge 50/50 -> 47/50 -> 10/50 and grimoire refreshed via BCP");

    // Target vitality HUD: ND_BCP_TARGET_HP (ref 7, 30/100) draws a 30% bar
    sock.cur!.send(bcp(18, [...u32(7), ...u16(30), ...u16(100)]));
    await page.waitForSelector("#combat-target-hud[data-target-pct='30']:not(.hidden)", { timeout: 4000 });
    const hudBar = await page.$eval("#combat-target-hud div[style*='width']", (el) => (el as HTMLElement).style.width);
    if (hudBar !== "30%") throw new Error(`Target HUD bar width should be 30%, got ${hudBar}`);
    sock.cur!.send(bcp(18, [...u32(0), ...u16(0), ...u16(0)])); // target gone
    await page.waitForSelector("#combat-target-hud.hidden", { state: "attached", timeout: 4000 });

    // Assert no alien ✕ button exists
    const hasCloseBtn = await page.evaluate(() => {
      const btns = Array.from(document.querySelectorAll("#spell-modal button"));
      return btns.some((b) => b.textContent?.includes("✕"));
    });
    if (hasCloseBtn) {
      throw new Error("Spellbook modal contains alien ✕ close button");
    }

    // Dismiss via backdrop click
    await page.locator("#spell-modal").click({ position: { x: 5, y: 5 } });
    await page.waitForSelector("#spell-modal", { state: "hidden", timeout: 3000 });

    // 5d. Equipment Modal (👕)
    const equipBtn = await page.waitForSelector('button[title="Equipment"]', { timeout: 3000 });
    await equipBtn.click();
    await page.waitForSelector("#equipment-modal:not(.hidden)", { timeout: 3000 });

    const equipSlots = await page.$$eval("#equipment-modal [data-slot]", (els) =>
      els.map((e) => e.getAttribute("data-slot"))
    );
    const expectedSlots = ["head", "neck", "chest", "back", "pants", "rhand", "lhand", "rfinger", "lfinger"];
    for (const s of expectedSlots) {
      if (!equipSlots.includes(s)) {
        throw new Error(`Equipment paper doll missing slot '${s}': ${JSON.stringify(equipSlots)}`);
      }
    }

    // Test unequip click on slot
    const headSlot = await page.waitForSelector('#equipment-modal [data-slot="head"]');
    await headSlot.click();
    await page.waitForTimeout(300);

    // Dismiss via backdrop click
    await page.locator("#equipment-modal").click({ position: { x: 5, y: 5 } });
    await page.waitForSelector("#equipment-modal", { state: "hidden", timeout: 3000 });

    // 5e. Character Sheet Modal (📋)
    const statsBtn = await page.waitForSelector('button[title="Character Sheet"]', { timeout: 3000 });
    await statsBtn.click();
    await page.waitForSelector("#stats-modal:not(.hidden)", { timeout: 3000 });

    const statsLabels = await page.$$eval("#stats-modal .tbbold", (els) =>
      els.map((e) => e.textContent?.trim())
    );
    const expectedStats = ["str", "con", "dex", "int", "wiz", "cha", "dodge", "dmg", "mdmg", "def", "mdef"];
    for (const stat of expectedStats) {
      if (!statsLabels.includes(stat)) {
        throw new Error(`Stats modal missing attribute '${stat}': ${JSON.stringify(statsLabels)}`);
      }
    }

    // Dismiss via backdrop click
    await page.locator("#stats-modal").click({ position: { x: 5, y: 5 } });
    await page.waitForSelector("#stats-modal", { state: "hidden", timeout: 3000 });

    // 5f. Dynamic server action buttons hygiene
    const gActionsTitles = await page.$$eval("#g-actions button", (els) =>
      els.map((e) => e.getAttribute("title")?.toLowerCase() || "")
    );
    if (gActionsTitles.length !== 6) {
      throw new Error(`Expected exactly 6 action buttons in #g-actions, got ${gActionsTitles.length}: ${JSON.stringify(gActionsTitles)}`);
    }
    if (gActionsTitles.includes("get") || gActionsTitles.includes("drop")) {
      throw new Error(`Action dock (#g-actions) erroneously contains Get or Drop: ${JSON.stringify(gActionsTitles)}`);
    }

    // Verify all action buttons have circular 32x32px geometry
    const actionGeoms = await page.$$eval('#g-actions button.round', (els) =>
      els.map((el) => {
        const cs = window.getComputedStyle(el);
        const rect = el.getBoundingClientRect();
        return { br: cs.borderRadius, w: Math.round(rect.width), h: Math.round(rect.height) };
      })
    );
    for (const g of actionGeoms) {
      if (g.br !== "9999px" && !g.br.includes("50%")) {
        throw new Error(`Action button not circular: borderRadius='${g.br}'`);
      }
    }

    // =========================================================================
    // STEP 6: Room Entity Context Actions (#contents)
    // =========================================================================
    const contentsEl = await page.waitForSelector("nd-contents#contents", { timeout: 5000 });
    if (!contentsEl) throw new Error("Missing nd-contents#contents element");

    let itemRows = await page.$$("nd-contents#contents nd-item[data-item-ref]");
    if (itemRows.length === 0) {
      // Inject room item entity fixture via WASM BCP channel
      await page.evaluate(() => {
        const bridge = (window as any).__bud_bridges?.nd;
        if (!bridge || !bridge.wasm) return;
        const wasm = bridge.wasm;
        const fn = wasm.wasm_ws_on_channel;
        const malloc = wasm.malloc;
        const free = wasm.free;
        if (!fn || !malloc || !free) return;

        // Construct BCP_ITEM buffer
        const buf = new Uint8Array(64);
        let p = 0;
        buf[p++] = 0x23; // '#'
        buf[p++] = 0x62; // 'b'
        buf[p++] = 0;    // ND_BCP_ITEM

        // ref = 9999
        buf[p++] = 0x0f; buf[p++] = 0x27; buf[p++] = 0; buf[p++] = 0;
        // location = 1001
        buf[p++] = 0xe9; buf[p++] = 0x03; buf[p++] = 0; buf[p++] = 0;
        // dynflags = 0
        buf[p++] = 0;
        // type = 3
        buf[p++] = 3; buf[p++] = 0; buf[p++] = 0; buf[p++] = 0;
        // actions = 5 (ACT_LOOK = 1 | ACT_GET = 4)
        buf[p++] = 5; buf[p++] = 0; buf[p++] = 0; buf[p++] = 0;

        // name = "iron key\0"
        const name = new TextEncoder().encode("iron key\0");
        buf.set(name, p);
        p += name.length;

        // unparse = "iron key\0"
        buf.set(name, p);
        p += name.length;

        // fg = 7 (u16)
        buf[p++] = 7; buf[p++] = 0;
        // flags = 0 (u16)
        buf[p++] = 0; buf[p++] = 0;
        // ch = ')' (u8)
        buf[p++] = 41;
        // art = "\0"
        buf[p++] = 0;

        const ptr = malloc(p);
        new Uint8Array(wasm.memory.buffer, ptr, p).set(buf.subarray(0, p));
        fn(1, ptr, p);
        free(ptr);
      });
      await page.waitForTimeout(500);
    }

    const firstItem = await page.waitForSelector("nd-contents#contents nd-item[data-item-ref]", { timeout: 5000 });
    if (!firstItem) throw new Error("No room item fixtures found in Citadel room");

    // Initially, item row must have zero visible action buttons displayed
    const initialButtons = await firstItem.$$("div:not(.hidden) button");
    if (initialButtons.length > 0) {
      throw new Error(`Expected 0 initial buttons on unselected items, found ${initialButtons.length}`);
    }

    // Click item row to select it
    await firstItem.click();
    await page.waitForTimeout(300);

    // Verify row becomes selected
    const isSelected = await firstItem.evaluate((el) => el.classList.contains("ctb"));
    if (!isSelected) {
      throw new Error("Clicked item row did not gain 'ctb' active selection class");
    }

    // Verify contextual actions unrolled (Look and Get for room item, no Drop)
    const rowButtons = await firstItem.$$eval("div:not(.hidden) button[data-cmd]", (els) =>
      els.map((b) => b.getAttribute("data-cmd") || "")
    );
    if (!rowButtons.some((c) => c.startsWith("look #"))) {
      throw new Error(`Selected item missing Look action: found ${JSON.stringify(rowButtons)}`);
    }
    if (!rowButtons.some((c) => c.startsWith("get #"))) {
      throw new Error(`Selected room item missing Get action: found ${JSON.stringify(rowButtons)}`);
    }
    if (rowButtons.some((c) => c.startsWith("drop #"))) {
      throw new Error(`Selected room item erroneously has Drop action: found ${JSON.stringify(rowButtons)}`);
    }

    // Click row again to unselect it
    await firstItem.click();
    await page.waitForTimeout(300);

    const unselectedButtons = await firstItem.$$("div:not(.hidden) button[data-cmd]");
    if (unselectedButtons.length > 0) {
      throw new Error(`Expected 0 buttons after unselecting item, found ${unselectedButtons.length}`);
    }

    // =========================================================================
    // STEP 7: Viewport Hardware Drivers Sequential Cycle
    // =========================================================================
    const cycleBtn = await page.waitForSelector('button[title="Switch Viewport"]', { timeout: 3000 });

    // Shared state: the HP gauge must read the same in every viewport
    const hpText = async () =>
      await page.$eval('[data-gauge="hp"]', (el) => (el.textContent || "").trim());
    const hpTty = await hpText();

    // Cycle 1: TTY -> 2D Lamina
    await cycleBtn.click();
    await page.waitForTimeout(500);

    const canvas2d = await page.waitForSelector("canvas#viewport", { timeout: 5000 });
    if (!canvas2d) throw new Error("canvas#viewport not mounted after switching to lamina-2d");
    await page.waitForTimeout(500);

    const hasPixels = await page.evaluate(() => {
      const canvas = document.querySelector("canvas#viewport") as HTMLCanvasElement;
      if (!canvas || canvas.width === 0 || canvas.height === 0) return false;
      const ctx = canvas.getContext("2d");
      if (!ctx) return false;
      const imgData = ctx.getImageData(0, 0, canvas.width, canvas.height);
      for (let i = 0; i < imgData.data.length; i += 4) {
        if (imgData.data[i] > 0 || imgData.data[i + 1] > 0 || imgData.data[i + 2] > 0) return true;
      }
      return false;
    });
    if (!hasPixels) throw new Error("Lamina 2D canvas is blank; room geometry not rendered");

    // Cycle 2: 2D Lamina -> back to TTY
    await cycleBtn.click();
    await page.waitForSelector("#term", { timeout: 5000 });
    if ((await hpText()) !== hpTty) throw new Error("HP gauge diverged after returning to TTY");

    console.log("ALL SEQUENTIAL CHECKS PASSED SUCCESSFULLY!");
  } finally {
    await context.close().catch(() => {});
    await browser.close().catch(() => {});
  }
});
