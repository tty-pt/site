
import { chromium } from "playwright";

const port = process.argv[2];
const browser = await chromium.launch({ args: ["--no-sandbox", "--disable-setuid-sandbox", "--disable-dev-shm-usage"] });
const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
const sent = [];
page.on("websocket", (ws) => ws.on("framesent", (f) => {
  const p = typeof f.payload === "string" ? f.payload : f.payload.toString("utf8");
  sent.push(p);
}));

const re = new RegExp("^[hjkl]\\n$");
const walkThroughGate = async (key) => {
  const before = sent.length;
  console.log("walkThroughGate", key, "starting, sent.length =", before);
  await page.keyboard.down(key);
  const t0 = Date.now();
  let hit = null;
  while (Date.now() - t0 < 6000 && !hit) {
    hit = sent.slice(before).find((p) => re.test(p)) || null;
    await page.waitForTimeout(50);
  }
  await page.keyboard.up(key);
  console.log("walkThroughGate", key, "done, hit =", hit);
  return hit;
};

await page.goto(`http://127.0.0.1:${port}/nd`, { waitUntil: "domcontentloaded" });
await page.waitForSelector("body[data-wasm-loaded]", { timeout: 15000 });
await page.waitForSelector("#term textarea", { state: "attached", timeout: 8000 });
await page.waitForTimeout(500);

// Go North: (0,0) -> (0,1)
await page.focus("#term textarea");
await page.keyboard.type("k");
await page.keyboard.press("Enter");
await page.waitForSelector("#seven-stage-canvas", { state: "attached", timeout: 10000 });
await page.waitForTimeout(1000);

// Walk Up to (0,2)
await walkThroughGate("ArrowUp");
await page.waitForTimeout(2000);

// Screenshot at start of ArrowDown
await page.screenshot({ path: "/tmp/down-0.png" });

// Start holding ArrowDown
await page.keyboard.down("ArrowDown");
await page.waitForTimeout(1000);
await page.screenshot({ path: "/tmp/down-1.png" });
await page.waitForTimeout(1000);
await page.screenshot({ path: "/tmp/down-2.png" });
await page.waitForTimeout(1000);
await page.screenshot({ path: "/tmp/down-3.png" });
await page.keyboard.up("ArrowDown");

console.log("Sent during ArrowDown:", sent.slice(4));

await browser.close();
