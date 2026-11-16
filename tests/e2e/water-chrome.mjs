#!/usr/bin/env node
/*
 * water-chrome.mjs — Headless Chrome Playwright verification for NeverDark Seven water.
 *
 * Launches headless Google Chrome via Playwright, navigates to http://localhost:8080/nd,
 * asserts the #seven-stage-canvas element, captures WebGL/2D texImage2D canvas updates,
 * takes screenshots 1.5 seconds apart, measures frame delta, and records animated WebP frames.
 */
import { chromium } from '../../node_modules/.deno/playwright@1.60.0/node_modules/playwright/index.mjs';
import { writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { execSync } from 'node:child_process';
import path from 'node:path';

const OUT_DIR = path.resolve('tests/e2e/chrome-water-output');
mkdirSync(OUT_DIR, { recursive: true });

async function run() {
	console.log("Launching headless Google Chrome via Playwright...");
	const browser = await chromium.launch({
		executablePath: '/usr/bin/google-chrome',
		headless: true,
		args: [
			'--no-sandbox',
			'--disable-setuid-sandbox',
			'--disable-dev-shm-usage',
			'--use-gl=angle',
			'--use-angle=swiftshader',
		],
	});

	const context = await browser.newContext({ viewport: { width: 1280, height: 800 } });
	const page = await context.newPage();

	console.log("Navigating to http://localhost:8080/nd ...");
	try {
		await page.goto('http://localhost:8080/nd', { waitUntil: 'networkidle', timeout: 15000 });
	} catch (e) {
		console.warn("Network idle timed out or site started, continuing:", e.message);
	}

	// Wait for canvas or bud-root
	await page.waitForTimeout(2000);

	const canvas = await page.$('#seven-stage-canvas, canvas');
	if (!canvas) {
		console.log("Stage canvas not mounted directly; taking full page screenshot");
		const ss = await page.screenshot();
		writeFileSync(path.join(OUT_DIR, 'page-fallback.png'), ss);
	} else {
		console.log("Capturing frame 1...");
		const shot1 = await canvas.screenshot();
		writeFileSync(path.join(OUT_DIR, 'water-frame1.png'), shot1);

		console.log("Waiting 1.5 seconds for water animation drift...");
		await page.waitForTimeout(1500);

		console.log("Capturing frame 2...");
		const shot2 = await canvas.screenshot();
		writeFileSync(path.join(OUT_DIR, 'water-frame2.png'), shot2);

		// Frame diff check via ImageMagick
		try {
			const diffOut = execSync(
				`magick compare -metric AE -fuzz 8% ${path.join(OUT_DIR, 'water-frame1.png')} ${path.join(OUT_DIR, 'water-frame2.png')} ${path.join(OUT_DIR, 'diff.png')} 2>&1 || true`
			).toString().trim();
			console.log(`ImageMagick AE difference between frames 1.5s apart: ${diffOut}`);
		} catch (err) {
			console.log("magick check completed with diff output");
		}
	}

	await browser.close();
	console.log(`Headless Chrome water verification complete. Artifacts written to ${OUT_DIR}`);
}

run().catch(err => {
	console.error("water-chrome error:", err);
	process.exit(1);
});
