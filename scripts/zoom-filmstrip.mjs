import { chromium } from "playwright";
import { mkdirSync, existsSync, writeFileSync } from "node:fs";
import { execSync } from "node:child_process";
import path from "node:path";

const port = process.argv[2] || 8080;
const OUT_DIR = path.resolve("tests/e2e/zoom-filmstrip");
mkdirSync(OUT_DIR, { recursive: true });

console.log(`[filmstrip] Launching Google Chrome against http://127.0.0.1:${port}/nd ...`);
const browser = await chromium.launch({
	executablePath: "/usr/bin/google-chrome",
	headless: true,
	args: [
		"--no-sandbox",
		"--disable-setuid-sandbox",
		"--disable-dev-shm-usage",
		"--use-gl=angle",
		"--use-angle=swiftshader"
	]
});

const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
page.on("console", msg => console.log("[page console]", msg.text()));
page.on("pageerror", err => console.log("[page error]", err));
page.on("response", res => { if (res.status() >= 400) console.log("[page res error]", res.status(), res.url()); });

try {
	await page.goto(`http://127.0.0.1:${port}/nd`, { waitUntil: "domcontentloaded", timeout: 15000 });
	await page.waitForSelector("body[data-wasm-loaded]", { timeout: 15000 });

	const hasStage = await page.$("#seven-stage-canvas");
	if (!hasStage) {
		const termArea = await page.waitForSelector("#term textarea, .xterm-helper-textarea, #term", { state: "attached", timeout: 8000 });
		await page.waitForTimeout(500);

		console.log("[filmstrip] Moving North into Seven stage...");
		await termArea.focus();
		await page.keyboard.type("k");
		await page.keyboard.press("Enter");
		await page.waitForSelector("#seven-stage-canvas", { state: "attached", timeout: 10000 });
	}
	await page.waitForTimeout(1500);

	const canvas = await page.$("#seven-stage-canvas");
	if (!canvas) {
		throw new Error("Could not find #seven-stage-canvas");
	}

	const frames = [];

	async function captureFrame(name, label) {
		await page.waitForTimeout(80);
		const filePath = path.join(OUT_DIR, `${name}.png`);
		const dataUrl = await page.evaluate(() => {
			const c = document.getElementById("seven-stage-canvas");
			return c ? c.toDataURL("image/png") : null;
		});
		if (dataUrl && dataUrl.startsWith("data:image/png;base64,")) {
			const buf = Buffer.from(dataUrl.slice("data:image/png;base64,".length), "base64");
			writeFileSync(filePath, buf);
		} else {
			await canvas.screenshot({ path: filePath, timeout: 5000 });
		}
		console.log(`[filmstrip] Captured ${name} (${label}) -> ${filePath}`);
		frames.push(filePath);
	}

	// 1. Reset zoom to 1.0
	await page.keyboard.press("0");
	await page.waitForTimeout(200);

	// Zoom in all the way (press + 6 times -> ~4.0x zoom)
	for (let i = 0; i < 6; i++) {
		await page.keyboard.press("+");
		await page.waitForTimeout(50);
	}
	await captureFrame("frame-01-zoom-4.0x", "Maximum Close-Up (4.0x)");

	// Step out to ~2.4x
	await page.keyboard.press("-");
	await page.keyboard.press("-");
	await captureFrame("frame-02-zoom-2.4x", "Close Inspection (2.4x)");

	// Step out to ~1.5x
	await page.keyboard.press("-");
	await page.keyboard.press("-");
	await captureFrame("frame-03-zoom-1.5x", "Slight Zoom-In (1.5x)");

	// Normal view (1.0x)
	await page.keyboard.press("0");
	await captureFrame("frame-04-zoom-1.0x", "Standard View (1.0x)");

	// Step out to ~0.75x
	await page.keyboard.press("-");
	await page.keyboard.press("-");
	await captureFrame("frame-05-zoom-0.75x", "Slight Pullback (0.75x)");

	// Step out to ~0.5x
	await page.keyboard.press("-");
	await page.keyboard.press("-");
	await captureFrame("frame-06-zoom-0.50x", "Half Scale (0.50x)");

	// Step out to ~0.35x
	await page.keyboard.press("-");
	await page.keyboard.press("-");
	await captureFrame("frame-07-zoom-0.35x", "Upper Atmosphere (0.35x)");

	// Step out to ~0.25x (stratospheric boundary)
	await page.keyboard.press("-");
	await page.keyboard.press("-");
	await captureFrame("frame-08-zoom-0.25x", "Stratospheric Boundary (0.25x)");

	// Step out to ~0.15x
	for (let i = 0; i < 3; i++) {
		await page.keyboard.press("-");
		await page.waitForTimeout(50);
	}
	await captureFrame("frame-09-zoom-0.15x", "Low Orbit (0.15x)");

	// Step out to ~0.10x
	for (let i = 0; i < 3; i++) {
		await page.keyboard.press("-");
		await page.waitForTimeout(50);
	}
	await captureFrame("frame-10-zoom-0.10x", "Far Orbit (0.10x)");

	// Step out to ~0.05x (multi-planet dispatch boundary)
	for (let i = 0; i < 4; i++) {
		await page.keyboard.press("-");
		await page.waitForTimeout(50);
	}
	await captureFrame("frame-11-zoom-0.05x", "High Orbit / Multi-Planet (0.05x)");

	// Step out to ~0.03x
	for (let i = 0; i < 4; i++) {
		await page.keyboard.press("-");
		await page.waitForTimeout(50);
	}
	await captureFrame("frame-12-zoom-0.03x", "Previous Minimum Horizon (0.03x)");

	// Step out to ~0.01x
	for (let i = 0; i < 6; i++) {
		await page.keyboard.press("-");
		await page.waitForTimeout(50);
	}
	await captureFrame("frame-13-zoom-0.01x", "Celestial Horizon (0.01x)");

	// Step out to ~0.005x
	for (let i = 0; i < 5; i++) {
		await page.keyboard.press("-");
		await page.waitForTimeout(50);
	}
	await captureFrame("frame-14-zoom-0.005x", "Macro Planetary Horizon (0.005x)");

	// Step out to ~0.001x
	for (let i = 0; i < 8; i++) {
		await page.keyboard.press("-");
		await page.waitForTimeout(50);
	}
	await captureFrame("frame-15-zoom-0.001x", "Deep Space Overview (0.001x)");

	// Step out to ~0.0006x
	for (let i = 0; i < 4; i++) {
		await page.keyboard.press("-");
		await page.waitForTimeout(50);
	}
	await captureFrame("frame-16-zoom-0.0006x", "Outer Space Torus Horizon (0.0006x)");

	// Step out to ~0.0002x
	for (let i = 0; i < 6; i++) {
		await page.keyboard.press("-");
		await page.waitForTimeout(50);
	}
	await captureFrame("frame-17-zoom-0.0002x", "Planetary Ring Torus Sector (0.0002x)");

	// Step out to ~0.00007x
	for (let i = 0; i < 6; i++) {
		await page.keyboard.press("-");
		await page.waitForTimeout(50);
	}
	await captureFrame("frame-18-zoom-0.00007x", "Macro Celestial Torus (0.00007x)");

	// Step out to ~0.00001x (minimum deep space zoom limit)
	for (let i = 0; i < 12; i++) {
		await page.keyboard.press("-");
		await page.waitForTimeout(50);
	}
	await captureFrame("frame-19-zoom-0.00001x", "Deep Space Closed Ring Torus (0.00001x)");

	// Step back into normal view to verify reversibility
	await page.keyboard.press("0");
	await captureFrame("frame-20-zoom-reset-1.0x", "Reset to Standard View (1.0x)");

	// Stitch frames into a filmstrip with thumbnails
	const filmstripPath = path.join(OUT_DIR, "filmstrip.png");
	try {
		const montageCmd = `montage ${frames.slice(0, 19).join(" ")} -tile 19x1 -geometry 320x200+2+2 ${filmstripPath}`;
		console.log(`[filmstrip] Assembling filmstrip: ${montageCmd}`);
		execSync(montageCmd);
		console.log(`[filmstrip] Filmstrip successfully generated at: ${filmstripPath}`);
	} catch (mErr) {
		console.log("[filmstrip] Montage command skipped or failed, frames preserved individually:", mErr.message);
	}

} catch (err) {
	console.error("[filmstrip] Error:", err);
	process.exitCode = 1;
} finally {
	await browser.close();
}
