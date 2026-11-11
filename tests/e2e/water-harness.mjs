#!/usr/bin/env node
/*
 * water-harness.mjs — runs the NeverDark Seven stage (htdocs/nd.wasm) in node,
 * with no browser and no server, to measure and capture the pure-C water.
 *
 *   node tests/e2e/water-harness.mjs --bench [--frames N] [--w 900 --h 656]
 *       ms per drawn frame for three scenes: land (no water), coast, deep.
 *       "water overlay" = scene ms - land ms.
 *   node tests/e2e/water-harness.mjs --dump DIR [--frames N]
 *       writes DIR/<scene>-<frame>.rgba (raw 0xAABBGGRR words) + .json meta.
 *   node tests/e2e/water-harness.mjs --parity
 *       server/browser parity: builds the ground renderer (mods/nd/ux
 *       nd_seven_ground.c + motion/objects/atlas_draw) natively (gcc -O2, as
 *       the SSR .so / unit tests) and as WASM (clang wasm32-wasi -O3 -flto
 *       -ffreestanding, as htdocs/nd.wasm), renders the same states (coast
 *       sand, coast grass, open sea; anim steps incl. int16 wrap; camera
 *       offsets) and requires byte-identical frames. The WASM build must have
 *       zero imports (no XXH32 / env.* stubs can hide a constant-noise bug).
 *   --json   machine-readable bench output.
 *
 * Imports: lamina.lam_canvas_flush captures the framebuffer; every other host
 * import is a no-op stub (no XXH32 import exists: libnoise inlines it).
 */
import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import os from 'node:os';
import { performance } from 'node:perf_hooks';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const has = k => args.includes(k);

const W = +opt('--w', 900), H = +opt('--h', 656);
const FRAMES = +opt('--frames', 120);
const WASM = opt('--wasm', path.join(ROOT, 'htdocs/nd.wasm'));

/* Biome indices used by the ND map tiles (see tests/unit/test_seven_world.c):
 * 0 = water, 10 = land (grass). */
const SCENES = {
	land:        () => 10,
	coast:       i => ((i % 7) <= 2 ? 10 : 0), /* land west, sea east */
	island:      i => { const c = i % 7, r = Math.floor(i / 7); return (c >= 2 && c <= 4 && r >= 2 && r <= 4) ? 14 : 0; }, /* tropical sand island */
	bay:         i => { const c = i % 7, r = Math.floor(i / 7); return (c >= 3 && r >= 2 && r <= 4) ? 0 : 10; }, /* coastal bay */
	deep:        () => 0,
	peninsula:   i => { const c = i % 7, r = Math.floor(i / 7); return (c === 3 && r >= 1 && r <= 4) ? 10 : 0; }, /* narrow finger of land */
	channel:     i => { const c = i % 7, r = Math.floor(i / 7); return (c >= 2 && c <= 3) ? 0 : 10; }, /* narrow strait between continents */
	diagonal:    i => { const c = i % 7, r = Math.floor(i / 7); return (c + r <= 6) ? 10 : 0; }, /* diagonal 45-degree coast */
	lake:        i => { const c = i % 7, r = Math.floor(i / 7); return (c >= 2 && c <= 4 && r >= 2 && r <= 4) ? 0 : 10; }, /* inland lake */
	archipelago: i => { const c = i % 7, r = Math.floor(i / 7); return ((c === 2 && r === 2) || (c === 4 && r === 3) || (c === 3 && r === 5)) ? 14 : 0; }, /* scattered islands */
};

function mapHex(scene, pos) {
	const b = Buffer.alloc(604);
	b[0] = 7; b[1] = 7; b[2] = 3; b[3] = 3;
	b.writeUInt32LE(1, 4);
	for (let k = 0; k < 4; k++) b.writeInt16LE(pos[k], 8 + 2 * k);
	for (let i = 0; i < 49; i++) {
		const o = 16 + 12 * i;
		b[o] = SCENES[scene](i);
		b[o + 1] = 0x1B; /* exits W N E S */
	}
	return b.toString('hex');
}

async function instance() {
	const bytes = readFileSync(WASM);
	let mem = null, flushes = 0, last = null;
	let terrainMeshFlushes = 0, terrainBytesUploaded = 0;
	let minimapFlushes = 0, stageFlushes = 0;
	const stub = () => 0;
	const imports = {
		axil: { ws_send: stub, ws_connect: stub },
		lamina: {
			lam_shader_bind: stub, lam_shader_uniforms: stub, lam_shader_register: () => 1,
			/* the minimap flushes too; track both */
			lam_canvas_flush: (buf, w, h) => {
				if (w * h < 256 * 256) {
					minimapFlushes++;
					return;
				}
				stageFlushes++;
				flushes++; last = { buf, w, h };
			},
			lam_terrain_mesh_flush: (col, hgt, cw, ch, sw, sh, sea, cp, water) => {
				flushes++; last = { buf: col, w: sw, h: sh };
				terrainMeshFlushes++;
				terrainBytesUploaded += (cw * ch * 6); /* 2 bytes height + 4 bytes color */
			},
			lam_billboard_batch_flush: stub,
			lam_cam_set_3d: stub,
			lam_cam_set_2d: stub,
		},
		xylem: { wasm_xy_load_module: stub, wasm_xy_unload_module: stub },
		stele: { stl_host_emit_patch: stub, stl_host_event_emit: stub },
		wasi_snapshot_preview1: {
			fd_close: stub, fd_seek: stub,
			fd_write: (fd, iov, n, out) => {
				const dv = new DataView(mem.buffer);
				let t = 0;
				for (let i = 0; i < n; i++) t += dv.getUint32(iov + 8 * i + 4, true);
				dv.setUint32(out, t, true);
				return 0;
			},
		},
	};
	/* fill any import the module adds later with a stub, so the harness
	 * keeps working; report it so new env.* imports are noticed. */
	for (const im of WebAssembly.Module.imports(new WebAssembly.Module(bytes))) {
		imports[im.module] ??= {};
		if (!(im.name in imports[im.module])) {
			console.error(`harness: stubbing unexpected import ${im.module}.${im.name}`);
			imports[im.module][im.name] = stub;
		}
	}
	const { instance: inst } = await WebAssembly.instantiate(bytes, imports);
	mem = inst.exports.memory;
	inst.exports._initialize?.();
	return {
		ex: inst.exports,
		get flushes() { return flushes; },
		get stageFlushes() { return stageFlushes; },
		get minimapFlushes() { return minimapFlushes; },
		get terrainMeshFlushes() { return terrainMeshFlushes; },
		get terrainBytesUploaded() { return terrainBytesUploaded; },
		frame() {
			if (!last) return null;
			return new Uint32Array(mem.buffer, last.buf, last.w * last.h).slice();
		},
		get size() { return last ? [last.w, last.h] : [0, 0]; },
		str(s) {
			const enc = Buffer.from(s + '\0');
			const p = inst.exports.malloc(enc.length);
			new Uint8Array(mem.buffer, p, enc.length).set(enc);
			return [p, enc.length - 1];
		},
	};
}

async function runScene(scene, frames, onFrame) {
	const h = await instance();
	const json = JSON.stringify({
		me: 1, user: 'harness', preferred_driver: 'lamina-seven',
		map_hex: mapHex(scene, [0, 0, 0, 0]), room_ref: 1,
	});
	const [p, n] = h.str(json);
	h.ex.wasm_init(p, n);
	/* warm-up: chunk bake, cache fill, first-frame allocation */
	const tWarmupStart = performance.now();
	for (let i = 0; i < 8; i++) h.ex.wasm_tick(1 / 60, W, H);
	const warmupMs = performance.now() - tWarmupStart;
	const f0 = h.flushes;
	const times = [];
	for (let i = 0; i < frames * 2; i++) {
		const t = performance.now();
		h.ex.wasm_tick(1 / 60, W, H);
		const dt = performance.now() - t;
		if (h.flushes !== f0 + times.length) { /* this tick drew */
			times.push(dt);
			onFrame?.(h, times.length - 1);
		}
	}
	times.sort((a, b) => a - b);
	const mean = times.reduce((a, b) => a + b, 0) / times.length;
	const uploadMBPerFrame = (h.terrainBytesUploaded / (times.length || 1)) / (1024 * 1024);
	return { scene, frames: times.length, mean, median: times[times.length >> 1],
		p95: times[Math.floor(times.length * 0.95)], size: h.size,
		warmup_ms: +warmupMs.toFixed(2),
		minimap_flushes_per_frame: +(h.minimapFlushes / (times.length || 1)).toFixed(1),
		upload_mb_per_frame: +uploadMBPerFrame.toFixed(2),
		mesh_flushes_per_frame: +(h.terrainMeshFlushes / (times.length || 1)).toFixed(1) };
}

async function bench() {
	const res = {};
	for (const s of Object.keys(SCENES)) res[s] = await runScene(s, FRAMES);
	const out = {
		size: `${W}x${H}`, frames: FRAMES,
		scenes: Object.fromEntries(Object.values(res).map(r =>
			[r.scene, { mean_ms: +r.mean.toFixed(3), median_ms: +r.median.toFixed(3),
				p95_ms: +r.p95.toFixed(3), drawn: r.frames, fb: r.size.join('x'),
				warmup_ms: r.warmup_ms,
				minimap_flushes_per_frame: r.minimap_flushes_per_frame,
				upload_mb_per_frame: r.upload_mb_per_frame,
				mesh_flushes_per_frame: r.mesh_flushes_per_frame }])),
		water_overlay_ms: {
			coast: +(res.coast.median - res.land.median).toFixed(3),
			deep: +(res.deep.median - res.land.median).toFixed(3),
		},
	};
	if (has('--json')) { console.log(JSON.stringify(out)); return; }
	console.log(`water-harness bench ${out.size}, ${FRAMES} drawn frames per scene`);
	for (const [k, v] of Object.entries(out.scenes))
		console.log(`  ${k.padEnd(6)} median ${v.median_ms} ms  mean ${v.mean_ms}  p95 ${v.p95_ms}  warmup ${v.warmup_ms} ms  minimap ${v.minimap_flushes_per_frame} flushes/f  upload ${v.upload_mb_per_frame} MB/f (${v.mesh_flushes_per_frame} chunks)`);
	console.log(`  water overlay (median vs land): coast ${out.water_overlay_ms.coast} ms, deep ${out.water_overlay_ms.deep} ms`);
	if (res.coast.size[0] === 0) { console.error('FAIL: no frame flushed'); process.exit(1); }
}

async function dump(dir) {
	mkdirSync(dir, { recursive: true });
	for (const s of Object.keys(SCENES)) {
		await runScene(s, FRAMES, (h, k) => {
			const [w, hh] = h.size;
			writeFileSync(path.join(dir, `${s}-${String(k).padStart(4, '0')}.rgba`),
				Buffer.from(h.frame().buffer));
			if (k === 0) writeFileSync(path.join(dir, `${s}.json`), JSON.stringify({ w, h: hh }));
		});
	}
	console.log(`dumped ${FRAMES} frames per scene to ${dir}`);
}

/* ---- parity ------------------------------------------------------------ */
const PARITY_C = String.raw`
#include <stdint.h>
#include <string.h>
#include <nd/bcp-types.h>
#include "nd_seven_ground.h"
#include "nd_seven_motion.h"
#ifdef __wasm__
#define EXPORT(n) __attribute__((export_name(n)))
#else
#define EXPORT(n)
#endif
#define PW TILE_PX_W
#define PH TILE_PX_H
static uint32_t fb[PW * PH];
static nd_client_state_t st;
static ndw_tile_t own;
/* scene: 0 coast sand, 1 coast grass, 2 open sea; each starts from a reset
 * cache and walks the same anim steps (13000 x 5 crosses the int16 wrap) */
static const int steps[] = { 0, 1, 2, 7, 64, 13000, 13000, 13000, 13000, 13000, 3, 1 };
static const int cam[][2] = { { 0, 0 }, { -3, -5 }, { 40, 17 } };
#define NSTEP (int)(sizeof(steps) / sizeof(steps[0]))
#define NCAM 3
static void scene(int k)
{
	nd_seven_ground_cache_forget();
	memset(&st, 0, sizeof(st));
	st.map_data.pos[3] = 1;
	st.map_data.width = st.map_data.height = 7;
	st.map_data.player_x = st.map_data.player_y = 3;
	for (int i = 0; i < 49; i++) {
		st.map_data.tiles[i].bio_idx = (k < 2 && (i % 7) <= 2) ? (k ? 10 : 14) : 0;
		st.map_data.tiles[i].exits = 0x1B;
	}
	own = nd_seven_convert_tile(&st.map_data.tiles[3 * 7 + 3]);
}
EXPORT("parity_count") int parity_count(void) { return 3 * NSTEP * NCAM; }
EXPORT("parity_fb") uint32_t *parity_fb(void) { return fb; }
/* frame i: renders and returns an FNV-1a hash of the frame */
EXPORT("parity_frame") uint32_t parity_frame(int i)
{
	static const int16_t pos[4] = { 0, 0, 0, 1 };
	int c = i % NCAM, s = (i / NCAM) % NSTEP, k = i / (NCAM * NSTEP);
	if (s == 0 && c == 0)
		scene(k);
	if (c == 0)
		nd_seven_step_anim((uint32_t)steps[s]);
	memset(fb, 0, sizeof(fb));
	nd_seven_ground_tile_render(fb, PW, PH, &st, 0, 0, pos, &own, cam[c][0], cam[c][1]);
	uint32_t h = 2166136261u;
	const uint8_t *b = (const uint8_t *)fb;
	for (size_t j = 0; j < sizeof(fb); j++)
		h = (h ^ b[j]) * 16777619u;
	return h;
}
#ifndef __wasm__
#include <stdio.h>
int main(int argc, char **argv)
{
	FILE *f = argc > 1 ? fopen(argv[1], "wb") : NULL;
	for (int i = 0; i < parity_count(); i++) {
		printf("%08x\n", parity_frame(i));
		if (f) fwrite(fb, sizeof(fb), 1, f);
	}
	if (f) fclose(f);
	return 0;
}
#endif
`;

async function parity() {
	const dir = mkdtempSync(path.join(os.tmpdir(), 'water-parity-'));
	const drv = path.join(dir, 'parity.c');
	writeFileSync(drv, PARITY_C);
	const inc = ['external/nd-world/include', 'external/libchaff/include', 'include',
		'external/axil-nd/include', 'external/nd-seven/include', 'external/libxylem/include',
		'mods/nd/ux'].map(d => '-I' + path.join(ROOT, d));
	const src = ['nd_seven_ground.c', 'nd_seven_motion.c', 'nd_seven_objects.c', 'nd_seven_atlas_draw.c']
		.map(f => path.join(ROOT, 'mods/nd/ux', f));
	const nat = path.join(dir, 'parity'), wasm = path.join(dir, 'parity.wasm'), raw = path.join(dir, 'native.rgba');
	try {
		execFileSync('gcc', ['-O2', ...inc, '-o', nat, drv, ...src, '-lxxhash'], { stdio: ['ignore', 'ignore', 'inherit'] });
		execFileSync('clang', ['--target=wasm32-wasi', '-O3', '-flto', '-ffreestanding', '-D__wasm__',
			'-fvisibility=hidden', ...inc, '-mexec-model=reactor', '-Wl,--no-entry', '-Wl,--gc-sections',
			'-o', wasm, drv, ...src], { stdio: ['ignore', 'ignore', 'pipe'] });
		const want = execFileSync(nat, [raw]).toString().trim().split('\n');
		const mod = new WebAssembly.Module(readFileSync(wasm));
		const imps = WebAssembly.Module.imports(mod);
		if (imps.length) {
			console.error('FAIL: parity wasm has imports:', imps.map(i => `${i.module}.${i.name}`).join(', '));
			process.exit(1);
		}
		const inst = new WebAssembly.Instance(mod, {});
		inst.exports._initialize?.();
		const nraw = readFileSync(raw), fsz = nraw.length / want.length;
		let bad = 0;
		const distinct = new Set();
		for (let i = 0; i < want.length; i++) {
			const got = (inst.exports.parity_frame(i) >>> 0).toString(16).padStart(8, '0');
			distinct.add(want[i]);
			if (got !== want[i]) {
				const a = new Uint8Array(inst.exports.memory.buffer, inst.exports.parity_fb(), fsz);
				const b = nraw.subarray(i * fsz, (i + 1) * fsz);
				let n = 0, first = -1;
				for (let j = 0; j < fsz; j += 4)
					if (a[j] !== b[j] || a[j + 1] !== b[j + 1] || a[j + 2] !== b[j + 2]) { n++; if (first < 0) first = j / 4; }
				console.error(`  frame ${i}: native ${want[i]} wasm ${got}: ${n} px differ, first at px ${first}`);
				bad++;
			}
		}
		console.log(`parity: ${want.length - bad}/${want.length} frames byte-identical native vs wasm ` +
			`(${distinct.size} distinct frames)`);
		if (bad || distinct.size < want.length / 2) {
			console.error(bad ? 'FAIL: native and wasm renders differ' : 'FAIL: frames do not vary (animation dead?)');
			process.exit(1);
		}
	} finally {
		rmSync(dir, { recursive: true, force: true });
	}
}

if (has('--parity')) await parity();
else if (has('--dump')) await dump(opt('--dump', 'water-frames'));
else await bench();
