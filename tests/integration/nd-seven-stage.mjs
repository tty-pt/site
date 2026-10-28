// Real-browser check of the 2D "seven" stage: walking north of the spawn
// enters the north-west region, where /nd swaps the terminal for the
// continuous canvas world. The stage must fill the page, draw from the sprite
// atlas, carry the mini-map on top of it, scroll as the lamb walks across
// tile borders (one move key per border, no gates) and give the terminal back
// at the spawn.
import { chromium } from "playwright";

const port = process.argv[2];
const browser = await chromium.launch({ args: ["--no-sandbox", "--disable-setuid-sandbox", "--disable-dev-shm-usage"] });
const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
const errors = [];
page.on("pageerror", (e) => errors.push(e.message));
const sent = [];
page.on("websocket", (ws) => ws.on("framesent", (f) => sent.push(String(f.payload))));
const fails = [];
const sleep = (ms) => page.waitForTimeout(ms);

const stats = (sel, name) => page.evaluate(([sel, name]) => {
  const c = document.querySelector(sel);
  if (!c) return null;
  const t = document.createElement("canvas");
  t.width = c.width; t.height = c.height;
  const x = t.getContext("2d");
  x.drawImage(c, 0, 0);
  const d = x.getImageData(0, 0, t.width, t.height).data;
  window.__snaps = window.__snaps || {};
  window.__snaps[name] = d;
  const seen = new Set();
  let green = 0;
  for (let i = 0; i < d.length; i += 4) {
    seen.add((d[i] << 16) | (d[i + 1] << 8) | d[i + 2]);
    if (d[i + 1] > d[i] && d[i + 1] > d[i + 2]) green++;
  }
  return { w: t.width, h: t.height, distinct: seen.size, green: green / (d.length / 4) };
}, [sel, name]);
const diff = (a, b) => page.evaluate(([a, b]) => {
  const x = window.__snaps[a], y = window.__snaps[b];
  let n = 0;
  for (let i = 0; i < x.length; i += 4)
    if (x[i] !== y[i] || x[i + 1] !== y[i + 1] || x[i + 2] !== y[i + 2]) n++;
  return n / (x.length / 4);
}, [a, b]);

// Hold an arrow until the stage asks the server to move (a tile border was
// crossed); keep it down for `extra` ms more and report every key it sent.
const walkAcross = async (key, extra = 0) => {
  const before = sent.length;
  await page.keyboard.down(key);
  const t0 = Date.now();
  let hit = null;
  while (Date.now() - t0 < 9000 && !hit) {
    hit = sent.slice(before).find((p) => /^[hjkl]\n$/.test(p)) || null;
    await sleep(100);
  }
  if (hit && extra) await sleep(extra);
  await page.keyboard.up(key);
  return { hit, keys: sent.slice(before).filter((p) => /^[hjkl]\n$/.test(p)) };
};

try {
  await page.goto(`http://127.0.0.1:${port}/nd`, { waitUntil: "domcontentloaded" });
  await page.waitForSelector("body[data-wasm-loaded]", { timeout: 15000 });
  await page.waitForSelector("#term textarea", { state: "attached", timeout: 8000 });
  await sleep(500);
  await page.focus("#term textarea");
  await page.keyboard.type("k");           // spawn (0,0) -> (0,1): north-west region
  await page.keyboard.press("Enter");
  await page.waitForSelector("#seven-stage-canvas", { state: "attached", timeout: 10000 })
    .catch(() => fails.push("seven stage did not appear after walking north of the spawn"));
  if (await page.$("#term")) fails.push("terminal still mounted on the 2D stage");
  // Ensure keyboard focus is on the document, not the hidden textarea
  await page.evaluate(() => { window.focus(); });
  await sleep(1200);

  const first = await stats("#seven-stage-canvas", "first");
  console.log("stage:", JSON.stringify(first));
  if (!first) fails.push("no stage canvas");
  else {
    const vp = page.viewportSize();
    if (first.w < vp.width * 0.7 || first.h < vp.height * 0.6)
      fails.push("stage canvas " + first.w + "x" + first.h + " does not fill the " + vp.width + "x" + vp.height + " page");
    if (first.distinct < 40) fails.push("stage is not sprite art (distinct colours " + first.distinct + ")");
  }
  const geo = await page.evaluate(() => {
    const r = (sel) => { const e = document.querySelector(sel); if (!e) return null; const b = e.getBoundingClientRect(); return { x: b.x, y: b.y, w: b.width, h: b.height, z: getComputedStyle(e).zIndex }; };
    return { stage: r("#seven-stage-canvas"), mini: r("#room-map-canvas"), overlay: r("#map-overlay") };
  });
  console.log("geometry:", JSON.stringify(geo));
  if (geo.stage && geo.mini) {
    const inside = geo.mini.x >= geo.stage.x && geo.mini.y >= geo.stage.y &&
      geo.mini.x + geo.mini.w <= geo.stage.x + geo.stage.w + 1 && geo.mini.y + geo.mini.h <= geo.stage.y + geo.stage.h + 1;
    if (!inside) fails.push("mini-map is not on top of the stage");
    if (!geo.overlay || geo.overlay.z !== "10") fails.push("mini-map overlay is not stacked above the stage");
    if (geo.mini.x + geo.mini.w < geo.stage.x + geo.stage.w * 0.5) fails.push("mini-map is not in the stage's right corner");
  }
  const mini = await stats("#room-map-canvas", "mini");
  console.log("mini :", JSON.stringify(mini));
  if (!mini || mini.distinct < 4) fails.push("mini-map canvas is blank (its frames went to the stage?)");
  else if (mini.w !== 224) fails.push("mini-map size " + mini.w);

  // Walk across a tile border in whichever direction exists: ONE move key per
  // border, the stage stays mounted and keeps scrolling, and the way back
  // works the same.
  const opposite = { ArrowUp: "ArrowDown", ArrowDown: "ArrowUp", ArrowLeft: "ArrowRight", ArrowRight: "ArrowLeft" };
  let crossed = null;
  for (const key of ["ArrowUp", "ArrowLeft", "ArrowRight", "ArrowDown"]) {
    const r = await walkAcross(key, 1200);
    if (r.hit) {
      crossed = key;
      if (r.keys.length !== 1) fails.push("crossing one border sent " + r.keys.length + " keys: " + JSON.stringify(r.keys));
      break;
    }
  }
  if (!crossed) fails.push("lamb could not walk across any tile border");
  else {
    await sleep(1500);
    const next = await stats("#seven-stage-canvas", "next");
    const d1 = await diff("first", "next");
    console.log("world scrolled by", d1.toFixed(3), "via", crossed);
    if (!next) fails.push("stage vanished after crossing a tile border");
    else if (d1 < 0.05) fails.push("the world did not scroll (diff " + d1 + ")");
    const back = await walkAcross(opposite[crossed], 0);
    if (!back.hit) fails.push("lamb could not walk back across the border");
  }
  // South until the spawn: the terminal returns when the stage region ends.
  for (let i = 0; i < 4 && !(await page.$("#term")); i++)
    await walkAcross("ArrowDown", 0);
  if (!(await page.$("#term")))
    await page.waitForSelector("#term textarea", { state: "attached", timeout: 8000 })
      .catch(() => fails.push("terminal did not return at the spawn"));
  if (await page.$("#seven-stage-canvas")) fails.push("seven stage still mounted at the spawn");
  if (errors.length) fails.push("page errors: " + errors.slice(0, 3).join(" | "));
} catch (e) {
  fails.push("exception: " + e.message);
}
await browser.close();
if (fails.length) { console.error("FAIL:\n - " + fails.join("\n - ")); process.exit(1); }
console.log("OK");
