// Real-browser check of the 2D "seven" stage: walking north of the spawn
// enters the north-west region, where /nd swaps the terminal for the Zelda-like
// canvas. The room must draw from the sprite atlas, differ from its neighbours,
// come back identical, and the mini-map canvas must keep its own frames.
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

// Hold an arrow until the stage asks the server to move (a gate was crossed).
const walkThroughGate = async (key) => {
  const before = sent.length;
  await page.keyboard.down(key);
  const t0 = Date.now();
  let hit = null;
  while (Date.now() - t0 < 7000 && !hit) {
    hit = sent.slice(before).find((p) => /^[hjkl]\n$/.test(p)) || null;
    await sleep(100);
  }
  await page.keyboard.up(key);
  return hit;
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
    if (first.w !== 640 || first.h !== 480) fails.push("stage canvas size " + first.w + "x" + first.h);
    if (first.distinct < 40) fails.push("stage is not sprite art (distinct colours " + first.distinct + ")");
    if (first.green < 0.2) fails.push("stage shows no grass (green " + first.green + ")");
  }
  const mini = await stats("#room-map-canvas", "mini");
  console.log("mini :", JSON.stringify(mini));
  if (!mini || mini.distinct < 4) fails.push("mini-map canvas is blank (its frames went to the stage?)");
  else if (mini.w !== 224) fails.push("mini-map size " + mini.w);

  // Walk out through whichever gate exists, look at the next room, walk back.
  const opposite = { ArrowUp: "ArrowDown", ArrowDown: "ArrowUp", ArrowLeft: "ArrowRight", ArrowRight: "ArrowLeft" };
  let crossed = null;
  for (const key of ["ArrowUp", "ArrowLeft", "ArrowRight", "ArrowDown"]) {
    const hit = await walkThroughGate(key);
    if (hit) { crossed = key; break; }
  }
  if (!crossed) fails.push("lamb could not leave through any gate");
  else {
    await sleep(2000);
    const next = await stats("#seven-stage-canvas", "next");
    const d1 = await diff("first", "next");
    console.log("next room differs from first by", d1.toFixed(3), "via", crossed);
    if (!next) fails.push("stage vanished after crossing a gate");
    else if (d1 < 0.25) fails.push("next room looks like the first (diff " + d1 + ")");
    const back = await walkThroughGate(opposite[crossed]);
    if (!back) fails.push("lamb could not walk back through the gate it came in by");
    else {
      await sleep(2000);
      await stats("#seven-stage-canvas", "again");
      const d2 = await diff("first", "again");
      console.log("revisited room differs from first by", d2.toFixed(3));
      if (d2 > 0.12) fails.push("revisiting the room gave a different layout (diff " + d2 + ")");
    }
  }
  // Out through the south gate of (0,1) back to the spawn: the terminal returns.
  const home = await walkThroughGate("ArrowDown");
  if (!home) fails.push("lamb could not walk back south to the spawn");
  else {
    await page.waitForSelector("#term textarea", { state: "attached", timeout: 8000 })
      .catch(() => fails.push("terminal did not return at the spawn"));
    if (await page.$("#seven-stage-canvas")) fails.push("seven stage still mounted at the spawn");
  }
  if (errors.length) fails.push("page errors: " + errors.slice(0, 3).join(" | "));
} catch (e) {
  fails.push("exception: " + e.message);
}
await browser.close();
if (fails.length) { console.error("FAIL:\n - " + fails.join("\n - ")); process.exit(1); }
console.log("OK");
