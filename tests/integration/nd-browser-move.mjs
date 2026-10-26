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
  if (/\x1b|\[4\dm/.test(back.map)) fails.push("raw ANSI leaked into the map");
  // The vim keys also work with focus outside the terminal (window keydown).
  await page.evaluate(() => document.activeElement && document.activeElement.blur());
  await page.keyboard.press("k");
  await page.waitForTimeout(2500);
  const key = await snap();
  console.log("key k :", JSON.stringify(key.head), "map", key.map.length);
  if (key.head === back.head) fails.push("pressing k outside the terminal did not move the map");
} catch (e) {
  fails.push("exception: " + e.message);
} finally {
  await browser.close();
}
if (process.env.ND_WS_LOG) console.log(wsLog.join("\n"));
if (errors.length) { console.log("page errors:", errors.slice(0, 5)); fails.push(errors.length + " page error(s)"); }
if (fails.length) { console.log("FAIL: " + fails.join("; ")); process.exit(1); }
console.log("ok: browser map and header follow moves");
