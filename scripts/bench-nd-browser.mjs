#!/usr/bin/env node
/**
 * scripts/bench-nd-browser.mjs — Headless Chrome GPU Pass & Frame-Pacing Benchmark for NeverDark Seven.
 *
 * Uses raw Chrome DevTools Protocol (CDP) over Node 24's built-in WebSocket (zero npm deps).
 * Automatically attaches to the Dedicated Worker running nd.wasm + webgl-cap.js (OffscreenCanvas)
 * and captures real rAF frame pacing (p50/p95/p99/max ms), WebGL FFI counts, and per-pass
 * EXT_disjoint_timer_query_webgl2 GPU timings from globalThis.__webglBridgePerf.
 *
 * Usage:
 *   node scripts/bench-nd-browser.mjs [--url http://127.0.0.1:8080/nd?stage=lamina-seven] [--frames 45] [--json]
 */

import { spawn } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const PASS_NAMES = {
	1: "CSM",
	2: "TERRAIN",
	3: "SKY",
	4: "HIZ",
	5: "TREE_SHADOW",
	6: "MESH3D_OPAQUE",
	7: "MESH3D_ALPHA",
	8: "BILLBOARDS",
	9: "GRASS",
	10: "WATER",
	11: "CLOUDS",
	12: "POST",
};

function parseArgs(argv) {
	const opts = {
		url: "http://127.0.0.1:8080/nd?stage=lamina-seven",
		frames: 45,
		json: false,
		chrome: process.env.CHROME_BIN || "/usr/bin/google-chrome",
	};
	for (let i = 2; i < argv.length; i++) {
		if (argv[i] === "--url" && argv[i + 1]) opts.url = argv[++i];
		else if (argv[i] === "--frames" && argv[i + 1]) opts.frames = Math.max(10, parseInt(argv[++i], 10) || 45);
		else if (argv[i] === "--json") opts.json = true;
		else if (argv[i] === "--chrome" && argv[i + 1]) opts.chrome = argv[++i];
	}
	return opts;
}

class CDPClient {
	constructor(wsUrl) {
		this.wsUrl = wsUrl;
		this.nextId = 1;
		this.pending = new Map();
		this.events = new Map();
	}
	async connect() {
		this.ws = new WebSocket(this.wsUrl);
		await new Promise((resolve, reject) => {
			this.ws.addEventListener("open", resolve, { once: true });
			this.ws.addEventListener("error", reject, { once: true });
		});
		this.ws.addEventListener("message", (ev) => {
			const msg = JSON.parse(typeof ev.data === "string" ? ev.data : ev.data.toString());
			if (msg.id && this.pending.has(msg.id)) {
				const { resolve, reject } = this.pending.get(msg.id);
				this.pending.delete(msg.id);
				if (msg.error) reject(new Error(`${msg.error.code}: ${msg.error.message}`));
				else resolve(msg.result);
			} else if (msg.method && this.events.has(msg.method)) {
				for (const fn of this.events.get(msg.method)) fn(msg.params, msg.sessionId);
			}
		});
	}
	send(method, params = {}, sessionId = undefined) {
		const id = this.nextId++;
		return new Promise((resolve, reject) => {
			this.pending.set(id, { resolve, reject });
			const payload = { id, method, params };
			if (sessionId) payload.sessionId = sessionId;
			this.ws.send(JSON.stringify(payload));
		});
	}
	on(method, fn) {
		if (!this.events.has(method)) this.events.set(method, []);
		this.events.get(method).push(fn);
	}
	async eval(expression, sessionId = undefined) {
		const res = await this.send(
			"Runtime.evaluate",
			{
				expression,
				awaitPromise: true,
				returnByValue: true,
			},
			sessionId,
		);
		if (res.exceptionDetails) {
			throw new Error(`Eval error: ${res.exceptionDetails.text} ${res.exceptionDetails.exception?.description || ""}`);
		}
		return res.result?.value;
	}
	close() {
		try { this.ws?.close(); } catch (_) {}
	}
}

async function launchChrome(chromeBin) {
	const userDataDir = fs.mkdtempSync(path.join(os.tmpdir(), "nd-bench-chrome-"));
	const hasDri = fs.existsSync("/dev/dri/renderD128");
	const args = [
		"--headless=new",
		"--remote-debugging-port=0",
		`--user-data-dir=${userDataDir}`,
		"--window-size=1280,720",
		"--no-sandbox",
		"--disable-setuid-sandbox",
		"--disable-dev-shm-usage",
		"--ignore-gpu-blocklist",
		"--enable-webgl",
		"--enable-webgl2",
		"--disable-background-timer-throttling",
		"--disable-renderer-backgrounding",
		"--disable-backgrounding-occluded-windows",
		...(hasDri
			? ["--use-gl=angle", "--use-angle=gl-egl", "--enable-unsafe-swiftshader"]
			: ["--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"]),
		"about:blank",
	];
	const proc = spawn(chromeBin, args, { stdio: ["ignore", "pipe", "pipe"] });
	let stderr = "";
	const wsEndpoint = await new Promise((resolve, reject) => {
		const timer = setTimeout(() => reject(new Error(`Chrome failed to start:\n${stderr}`)), 15000);
		proc.stderr.on("data", (chunk) => {
			stderr += chunk.toString();
			const m = stderr.match(/DevTools listening on (ws:\/\/[^\s]+)/);
			if (m) {
				clearTimeout(timer);
				resolve(m[1]);
			}
		});
		proc.on("exit", (code) => {
			clearTimeout(timer);
			reject(new Error(`Chrome exited early (${code}):\n${stderr}`));
		});
	});
	const port = new URL(wsEndpoint).port;
	const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
	const pageTarget = targets.find((t) => t.type === "page");
	if (!pageTarget) throw new Error("No page target found");
	return {
		proc,
		pageWsUrl: pageTarget.webSocketDebuggerUrl,
		cleanup: () => {
			try { proc.kill("SIGKILL"); } catch (_) {}
			try { fs.rmSync(userDataDir, { recursive: true, force: true }); } catch (_) {}
		},
	};
}

function summarize(deltas) {
	const s = [...deltas].sort((a, b) => a - b);
	const pick = (p) => s[Math.min(s.length - 1, Math.floor(s.length * p))] ?? 0;
	const avg = s.reduce((a, b) => a + b, 0) / (s.length || 1);
	return {
		frames: s.length,
		avgMs: +avg.toFixed(2),
		p50Ms: +pick(0.50).toFixed(2),
		p95Ms: +pick(0.95).toFixed(2),
		p99Ms: +pick(0.99).toFixed(2),
		maxMs: +(s[s.length - 1] ?? 0).toFixed(2),
		fps: +(avg > 0 ? 1000 / avg : 0).toFixed(1),
	};
}

function formatPasses(gpuPasses) {
	if (!gpuPasses) return {};
	const out = {};
	for (const [id, st] of Object.entries(gpuPasses)) {
		const name = PASS_NAMES[id] || `PASS_${id}`;
		out[name] = {
			avgMs: +(st.avgMs ?? 0).toFixed(3),
			maxMs: +(st.maxMs ?? 0).toFixed(3),
			samples: st.samples ?? 0,
		};
	}
	return out;
}

async function runPhase(cdp, perfSessionId, name, frameCount, setupFnExpr, teardownFnExpr) {
	if (setupFnExpr) await cdp.eval(setupFnExpr);
	const res = await cdp.eval(
		`(async () => {
			const p = globalThis.__webglBridgePerf;
			if (p) { p.enable(true); p.reset(); }
			const deltas = [];
			let last = performance.now();
			for (let i = 0; i < ${frameCount}; i++) {
				await new Promise((r) => requestAnimationFrame(r));
				const now = performance.now();
				deltas.push(now - last);
				last = now;
			}
			for (let i = 0; i < 4; i++) await new Promise((r) => requestAnimationFrame(r));
			const snap = p ? p.snapshot() : null;
			return { deltas, snap };
		})()`,
		perfSessionId,
	);
	if (teardownFnExpr) await cdp.eval(teardownFnExpr);
	const pacing = summarize(res.deltas);
	const snap = res.snap || {};
	const f = Math.max(1, frameCount + 4);
	return {
		name,
		pacing,
		perFrame: {
			draws: +((snap.draws || 0) / f).toFixed(1),
			uniforms: +((snap.uniformUploads || 0) / f).toFixed(1),
			cmdFlushes: +((snap.cmdFlushes || 0) / f).toFixed(1),
			cmdOps: +((snap.cmdOps || 0) / f).toFixed(1),
			texUploads: +((snap.texUploads || 0) / f).toFixed(1),
			bufUploads: +((snap.bufferUploads || 0) / f).toFixed(1),
			blits: +((snap.blits || 0) / f).toFixed(1),
		},
		gpuPasses: formatPasses(snap.gpuPasses),
	};
}

async function main() {
	const opts = parseArgs(process.argv);
	const chrome = await launchChrome(opts.chrome);
	const cdp = new CDPClient(chrome.pageWsUrl);
	const pageErrors = [];
	const workerSessions = [];

	try {
		await cdp.connect();
		await cdp.send("Page.enable");
		await cdp.send("Runtime.enable");
		await cdp.send("Emulation.setDeviceMetricsOverride", {
			width: 1280,
			height: 720,
			deviceScaleFactor: 1,
			mobile: false,
		});
		cdp.on("Runtime.exceptionThrown", (p) => {
			pageErrors.push(p.exceptionDetails?.text || "exception");
		});
		cdp.on("Target.attachedToTarget", (p) => {
			if (p.targetInfo?.type === "worker" && p.sessionId) {
				workerSessions.push({ sessionId: p.sessionId, url: p.targetInfo.url });
				cdp.send("Runtime.enable", {}, p.sessionId).catch(() => {});
			}
		});
		await cdp.send("Target.setAutoAttach", {
			autoAttach: true,
			waitForDebuggerOnStart: false,
			flatten: true,
		});

		await cdp.send("Page.navigate", { url: opts.url });

		// Probe GL capabilities using a fresh probe canvas on page (never touching transferred OffscreenCanvas)
		// and wait for __webglBridgePerf in either a worker session or main page.
		let perfSessionId = undefined;
		let perfContext = "none";
		const deadline = Date.now() + 20000;
		while (Date.now() < deadline) {
			for (const w of workerSessions) {
				try {
					const ok = await cdp.eval("!!globalThis.__webglBridgePerf", w.sessionId);
					if (ok) {
						perfSessionId = w.sessionId;
						perfContext = `worker(${w.url.split("/").pop()})`;
						break;
					}
				} catch (_) {}
			}
			if (perfContext !== "none") break;
			try {
				const ok = await cdp.eval("!!globalThis.__webglBridgePerf");
				if (ok) {
					perfSessionId = undefined;
					perfContext = "page";
					break;
				}
			} catch (_) {}
			await new Promise((r) => setTimeout(r, 200));
		}

		const envInfo = await cdp.eval(`(() => {
			const c = document.querySelector("canvas");
			const canvasSize = c ? (c.width + "x" + c.height) : "none";
			const probe = document.createElement("canvas");
			const gl = probe.getContext("webgl2");
			let renderer = "unknown", vendor = "unknown", hasTimerExt = false;
			if (gl) {
				const dbg = gl.getExtension("WEBGL_debug_renderer_info");
				renderer = dbg ? gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER);
				vendor = dbg ? gl.getParameter(dbg.UNMASKED_VENDOR_WEBGL) : gl.getParameter(gl.VENDOR);
				hasTimerExt = !!gl.getExtension("EXT_disjoint_timer_query_webgl2");
			}
			return { canvasSize, renderer, vendor, hasTimerExt, url: location.href };
		})()`);
		envInfo.perfContext = perfContext;

		if (perfContext === "none") {
			throw new Error(`__webglBridgePerf not found in page or ${workerSessions.length} workers on ${envInfo.url}`);
		}

		// Wait until Seven has rendered initial frames, then warm up 15 frames
		await cdp.eval(
			`(async () => {
				const p = globalThis.__webglBridgePerf;
				p.enable(true);
				const t0 = performance.now();
				while (performance.now() - t0 < 12000) {
					const s = p.snapshot();
					if ((s.draws || 0) >= 100) break;
					await new Promise((r) => requestAnimationFrame(r));
				}
				for (let i = 0; i < 15; i++) await new Promise((r) => requestAnimationFrame(r));
			})()`,
			perfSessionId,
		);

		// Phase 1: Stationary 1.0x
		const p1 = await runPhase(cdp, perfSessionId, "stationary_1x", opts.frames, null, null);

		// Phase 2: Panning (hold ArrowRight on page)
		const p2 = await runPhase(
			cdp,
			perfSessionId,
			"panning_1x",
			opts.frames,
			`(() => { window.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowRight", code: "ArrowRight", bubbles: true })); })()`,
			`(() => { window.dispatchEvent(new KeyboardEvent("keyup", { key: "ArrowRight", code: "ArrowRight", bubbles: true })); })()`,
		);

		// Phase 3: Zoom-out sweep (wheel events on canvas/stage)
		const p3 = await runPhase(
			cdp,
			perfSessionId,
			"zoom_sweep",
			opts.frames,
			`(() => {
				const el = document.querySelector("canvas") || document.body;
				let step = 0;
				window.__benchZoomTimer = setInterval(() => {
					if (step++ >= 22) { clearInterval(window.__benchZoomTimer); return; }
					el.dispatchEvent(new WheelEvent("wheel", { deltaY: 160, bubbles: true, cancelable: true }));
				}, 32);
			})()`,
			`(() => { if (window.__benchZoomTimer) clearInterval(window.__benchZoomTimer); })()`,
		);

		// Phase 4: Wide-zoom steady state
		await cdp.eval(
			`(async () => { for (let i = 0; i < 15; i++) await new Promise((r) => requestAnimationFrame(r)); })()`,
			perfSessionId,
		);
		const p4 = await runPhase(cdp, perfSessionId, "wide_zoom_steady", opts.frames, null, null);

		const report = { env: envInfo, phases: [p1, p2, p3, p4], pageErrors };
		if (opts.json) {
			console.log(JSON.stringify(report, null, 2));
		} else {
			console.log(`=== NeverDark Seven Browser Benchmark ===`);
			console.log(`URL:      ${envInfo.url}`);
			console.log(`Canvas:   ${envInfo.canvasSize}  Host: ${envInfo.perfContext}`);
			console.log(`Renderer: ${envInfo.renderer} (${envInfo.vendor})`);
			console.log(`GPU Timer (EXT_disjoint_timer_query_webgl2): ${envInfo.hasTimerExt ? "YES" : "NO"}`);
			if (pageErrors.length) console.log(`Page errors: ${pageErrors.join("; ")}`);
			console.log("");
			console.log("Phase              |  fps | avg_ms | p50_ms | p95_ms | p99_ms | max_ms | draws | unif  | cmdFl | cmdOp | texUp");
			console.log("-------------------+------+--------+--------+--------+--------+--------+-------+-------+-------+-------+------");
			for (const ph of report.phases) {
				const p = ph.pacing, f = ph.perFrame;
				console.log(
					`${ph.name.padEnd(18)} | ${String(p.fps).padStart(4)} | ${String(p.avgMs).padStart(6)} | ${String(p.p50Ms).padStart(6)} | ${String(p.p95Ms).padStart(6)} | ${String(p.p99Ms).padStart(6)} | ${String(p.maxMs).padStart(6)} | ${String(f.draws).padStart(5)} | ${String(f.uniforms).padStart(5)} | ${String(f.cmdFlushes).padStart(5)} | ${String(f.cmdOps).padStart(5)} | ${String(f.texUploads).padStart(5)}`
				);
			}
			console.log("\nPer-Pass GPU Timings (ms avg / max):");
			for (const ph of report.phases) {
				const entries = Object.entries(ph.gpuPasses);
				if (!entries.length) {
					console.log(`  ${ph.name}: (no GPU timer samples)`);
					continue;
				}
				const sumAvg = entries.reduce((a, [, v]) => a + v.avgMs, 0);
				const line = entries
					.sort((a, b) => b[1].avgMs - a[1].avgMs)
					.map(([k, v]) => `${k}=${v.avgMs.toFixed(2)}ms(max ${v.maxMs.toFixed(2)})`)
					.join("  ");
				console.log(`  ${ph.name} [sum=${sumAvg.toFixed(2)}ms]: ${line}`);
			}
		}
	} finally {
		cdp.close();
		chrome.cleanup();
	}
}

main().catch((err) => {
	console.error(err);
	process.exit(1);
});
