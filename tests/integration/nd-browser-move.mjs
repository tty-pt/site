// Drives a real headless Chrome against a local /nd and checks that a vim move
// refreshes the page's map and room header, not just the terminal.
import { chromium } from "playwright";

const port = process.argv[2];
const url = `http://127.0.0.1:${port}/nd`;
const browser = await chromium.launch({ args: ["--no-sandbox", "--disable-setuid-sandbox", "--disable-dev-shm-usage"] });
const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
const errors = [];
page.on("pageerror", (e) => errors.push(e.message));
page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
const fails = [];
const wsLog = [];
page.on("websocket", (ws) => {
  wsLog.push("open " + ws.url());
  ws.on("framesent", (f) => wsLog.push("sent " + JSON.stringify(String(f.payload).slice(0, 40))));
  ws.on("framereceived", (f) => {
    const b = typeof f.payload === "string" ? Buffer.from(f.payload) : f.payload;
    wsLog.push("recv " + (b[0] === 0x23 && b[1] === 0x62 ? "BCP" + b[2] : "txt") + " len" + b.length);
  });
  ws.on("close", () => wsLog.push("closed"));
});
try {
  await page.goto(url, { waitUntil: "domcontentloaded" });
  await page.waitForSelector("body[data-wasm-loaded]", { timeout: 15000 }).catch((e) => { throw new Error("wasm not loaded: " + e.message); });
  await page.waitForSelector("nd-map#room-map", { state: "attached", timeout: 8000 });
  await page.waitForFunction(() => (document.querySelector("nd-map#room-map")?.textContent || "").length > 20, null, { timeout: 8000 });
  const snap = () => page.evaluate(() => ({
    map: document.querySelector("nd-map#room-map")?.innerHTML || "",
    head: document.querySelector("#room-header")?.textContent || "",
    term: Array.from(document.querySelectorAll("#term .xterm-rows > div")).map((r) => r.textContent).join("\n"),
  }));
  // The player types into the xterm terminal, exactly like the live page.
  const sendLine = async (line) => {
    await page.focus("#term textarea");
    await page.keyboard.type(line);
    await page.keyboard.press("Enter");
  };
  const before = await snap();
  console.log("before:", JSON.stringify(before.head), "map", before.map.length);
  await page.waitForTimeout(500);
  await sendLine("k");
  await page.waitForTimeout(2500);
  const north = await snap();
  console.log("north :", JSON.stringify(north.head), "map", north.map.length);
  if (!/go .*north/i.test(north.term)) fails.push("terminal did not show the move north");
  if (north.head === before.head) fails.push("room header did not change after moving north");
  if (north.map === before.map) fails.push("map did not change after moving north");
  await sendLine("j");
  await page.waitForTimeout(2500);
  const back = await snap();
  console.log("south :", JSON.stringify(back.head), "map", back.map.length);
  if (back.head !== before.head) fails.push("room header did not return after moving south");
  if (back.map === north.map) fails.push("map did not change after moving back south");
  if (back.map !== before.map) fails.push("map after moving back south differs from initial login map");
  if (/\x1b|\[4\dm/.test(back.map)) fails.push("raw ANSI leaked into the map");
  // The vim keys also work with focus outside the terminal (window keydown).
  await page.evaluate(() => document.activeElement && document.activeElement.blur());
  await page.keyboard.press("k");
  await page.waitForTimeout(2500);
  const key = await snap();
  console.log("key k :", JSON.stringify(key.head), "map", key.map.length);
  if (key.head === back.head) fails.push("pressing k outside the terminal did not move the map");
  // Return to spawn room #2 for fixture tests
  await page.keyboard.press("j");
  await page.waitForTimeout(2500);
  // Layout: the map and the looked-at object's image are never shown together,
  // and the direction pad is for small/touch screens only.
  const vis = (sel) => page.evaluate((q) => {
    const el = document.querySelector(q);
    return !!el && getComputedStyle(el).display !== "none" && el.getClientRects().length > 0;
  }, sel);
  if (await vis("#directions")) fails.push("direction pad visible on a large screen");
  if (!(await vis("nd-map#room-map"))) fails.push("map hidden although nothing is targeted");
  await sendLine("inventory");
  await page.waitForSelector("#target-card", { state: "attached", timeout: 8000 })
    .catch(() => fails.push("target card did not appear after inventory"));
  if (!(await vis("#target-card"))) fails.push("target card not visible when targeting");
  if (await vis("nd-map#room-map")) fails.push("map still visible together with the target card");
  // Look-at image is roughly map-sized (not a chip, not a poster), titled by overlay.
  const art = await page.evaluate(() => {
    const r = (q) => { const e = document.querySelector(q); if (!e) return null; const b = e.getBoundingClientRect(); return { w: b.width, h: b.height }; };
    return { art: r("#target-card .target-art"), title: !!document.querySelector("#target-card .target-title") };
  });
  if (!art.art || Math.abs(art.art.w - art.art.h) > 2) fails.push("target art is not square: " + JSON.stringify(art.art));
  else if (art.art.w < 200 || art.art.w > 330) fails.push("target art is not map-sized: " + art.art.w);
  if (!art.title) fails.push("target name not overlaid on the art");
  await page.click("#target-card .target-close");
  await page.waitForFunction(() => !document.querySelector("#target-card"), null, { timeout: 5000 })
    .catch(() => fails.push("dismiss did not clear the target card"));
  // Panel layout parity: centered global actions, unlabelled roomy contents rows.
  const lay = await page.evaluate(() => {
    const a = document.querySelector("#g-actions").getBoundingClientRect();
    const p = document.querySelector("#g-actions").parentElement.getBoundingClientRect();
    const m = document.querySelector("nd-map#room-map").getBoundingClientRect();
    const c = document.querySelector("#contents");
    const row = c.querySelector("nd-item");
    const cs = row && getComputedStyle(row);
    return {
      offcenter: Math.abs((a.left + a.width / 2) - (p.left + p.width / 2)),
      mapW: m.width,
      label: /room contents|inventory/i.test(c.textContent),
      avatar: !!(row && row.querySelector("nd-avatar")),
      border: cs ? parseFloat(cs.borderBottomWidth) : 0,
      radius: cs ? parseFloat(cs.borderTopLeftRadius) : 0,
      cbottom: c.getBoundingClientRect().bottom,
      abottom: a.top,
    };
  });
  if (lay.offcenter > 4) fails.push("global actions not centered: off by " + lay.offcenter);
  if (lay.label) fails.push("contents list still has a label");
  if (!lay.avatar) fails.push("contents rows have no avatar");
  if (lay.border > 0) fails.push("contents rows still have borders");
  if (lay.radius < 6) fails.push("contents rows are not rounded");
  if (lay.cbottom > lay.abottom) fails.push("contents list overlaps the action bar");
  if (art.art && Math.abs(art.art.w - lay.mapW) > lay.mapW * 0.35) fails.push(`target art ${art.art.w} vs map ${lay.mapW} differ too much`);

  // Item selection: clicking an item reveals a floating action popup capsule (position: absolute)
  await page.click("#contents nd-item");
  await page.waitForTimeout(300);
  const popup = await page.evaluate(() => {
    const p = document.querySelector("#contents nd-item .nd-item-popup");
    if (!p) return null;
    const cs = getComputedStyle(p);
    return {
      pos: cs.position,
      vis: cs.display !== "none" && p.getClientRects().length > 0,
      look: !!p.querySelector("[data-action='look']"),
      get: !!p.querySelector("[data-action='get']"),
    };
  });
  if (!popup || !popup.vis) fails.push("item action popup did not appear on item click");
  else {
    if (popup.pos !== "absolute") fails.push("item action popup is not position: absolute: " + popup.pos);
    if (!popup.look) fails.push("item action popup missing look button");
  }

  const small = await browser.newPage({ viewport: { width: 390, height: 800 } });
  await small.goto(url, { waitUntil: "domcontentloaded" });
  await small.waitForSelector("#directions", { state: "attached", timeout: 8000 });
  const smallVis = await small.evaluate(() => getComputedStyle(document.querySelector("#directions")).display);
  if (smallVis === "none") fails.push("direction pad hidden on a small screen");
} catch (e) {
  fails.push("exception: " + e.message);
} finally {
  await browser.close();
}
if (process.env.ND_WS_LOG) console.log(wsLog.join("\n"));
if (errors.length) { console.log("page errors:", errors.slice(0, 5)); fails.push(errors.length + " page error(s)"); }
if (fails.length) { console.log("FAIL: " + fails.join("; ")); process.exit(1); }
console.log("ok: browser map and header follow moves");
