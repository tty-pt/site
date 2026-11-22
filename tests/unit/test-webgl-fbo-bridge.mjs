// WebGL bridge offscreen render-target API: framebuffer / renderbuffer / blit routing,
// attachment bookkeeping, and rebuild when the GL context changes (canvas swap).
import assert from 'node:assert/strict';

function makeGl(log) {
	let id = 0;
	let unpackAlignment = 4;
	const attached = new WeakMap();
	const linked = new WeakSet();
	let activeProgram = null;
	const mk = (kind) => ({ kind, n: ++id });
	const gl = {
		FRAMEBUFFER: 0x8D40, RENDERBUFFER: 0x8D41,
		ARRAY_BUFFER: 0x8892, ELEMENT_ARRAY_BUFFER: 0x8893,
		UNPACK_ALIGNMENT: 0x0CF5, LINK_STATUS: 0x8B82,
		createFramebuffer: () => mk('fb'), createRenderbuffer: () => mk('rb'), createTexture: () => mk('tex'),
		createBuffer: () => mk('buf'),
		createShader: (type) => Object.assign(mk('sh'), { type }),
		createProgram: () => mk('prog'),
		shaderSource: () => {}, compileShader: () => {}, deleteShader: () => {},
		attachShader: (p, s) => {
			let set = attached.get(p);
			if (!set) { set = new Set(); attached.set(p, set); }
			if (set.has(s)) throw new Error('attachShader: shader attachment already has shader');
			set.add(s);
			log.push(['attachShader', p.n, s.n]);
		},
		bindAttribLocation: () => {},
		linkProgram: (p) => { linked.add(p); log.push(['linkProgram', p.n]); },
		getProgramParameter: (p, pname) => pname === 0x8B82 ? linked.has(p) : true,
		useProgram: (p) => {
			if (p && !linked.has(p)) throw new Error('useProgram: program is not linked');
			activeProgram = p;
			log.push(['useProgram', p ? p.n : null]);
		},
		getUniformLocation: (p, name) => linked.has(p) ? { prog: p.n, name } : null,
		uniform1i: (loc, v) => log.push(['uniform1i', loc ? loc.prog : 0, loc ? loc.name : '', v]),
		drawArraysInstanced: (m, f, c, ic) => {
			if (!activeProgram || !linked.has(activeProgram)) throw new Error('drawArraysInstanced: current program is not linked');
			log.push(['drawArraysInstanced', activeProgram.n, ic]);
		},
		bindBuffer: (t, b) => log.push(['bindBuffer', t, b ? b.n : null]),
		bufferData: () => {},
		pixelStorei: (p, v) => { if (p === 0x0CF5) unpackAlignment = v; log.push(['pixelStorei', p, v]); },
		deleteFramebuffer: (f) => log.push(['deleteFramebuffer', f.n]),
		deleteRenderbuffer: (r) => log.push(['deleteRenderbuffer', r.n]),
		bindFramebuffer: (t, f) => log.push(['bindFramebuffer', t, f ? f.n : null]),
		bindRenderbuffer: (t, r) => log.push(['bindRenderbuffer', t, r ? r.n : null]),
		bindTexture: () => {}, texParameteri: () => {},
		texImage2D: (...a) => {
			const px = a[8];
			if (px && a[3] > 0 && a[4] > 0) {
				const ch = (a[6] === 0x1903 || a[6] === 0x1902 || a[6] === 0x8D94) ? 1 : (a[6] === 0x1907 ? 3 : 4);
				const bpe = a[7] === 0x1402 ? 2 : (a[7] === 0x1406 ? 4 : 1);
				const rowBytes = a[3] * ch * bpe;
				const stride = Math.ceil(rowBytes / unpackAlignment) * unpackAlignment;
				const needed = (a[4] - 1) * stride + rowBytes;
				if (px.byteLength < needed) {
					throw new Error(`texImage: Desired upload requires more bytes (${needed}) than are available (${px.byteLength})`);
				}
			}
			log.push(['texImage2D', a[0], a[1], a[2], a[3], a[4], px ? px.byteLength : 0, unpackAlignment]);
		},
		texSubImage2D: (...a) => log.push(['texSubImage2D', a[0], a[1], a[2], a[3], a[4], a[5], a[8] ? a[8].byteLength : 0]),
		renderbufferStorageMultisample: (t, s, f, w, h) => log.push(['rbStorageMS', s, f, w, h]),
		framebufferRenderbuffer: (t, a, rt, r) => log.push(['fbRb', a, r ? r.n : null]),
		framebufferTexture2D: (t, a, tt, tex, l) => log.push(['fbTex', a, tex ? tex.n : null, l]),
		checkFramebufferStatus: () => 0x8CD5,
		blitFramebuffer: (...a) => log.push(['blit', ...a]),
		stencilFunc: (...a) => log.push(['stencilFunc', ...a]), stencilOp: (...a) => log.push(['stencilOp', ...a]),
		stencilMask: (...a) => log.push(['stencilMask', ...a]), clearStencil: (...a) => log.push(['clearStencil', ...a]),
		viewport: (...a) => log.push(['viewport', ...a]),
		getExtension: () => null, activeTexture: () => {},
	};
	return gl;
}

const log = [];
const glA = makeGl(log);
const glB = makeGl(log);
let current = glA;
let canvasA = { isConnected: true, getContext: () => glA };
let canvasB = { isConnected: true, getContext: () => glB };
let canvas = canvasA;
globalThis.document = { querySelector: () => canvas, querySelectorAll: () => [canvas] };
globalThis.requestAnimationFrame = () => {};
const { createWebGlBridge } = await import('../../external/liblamina/htdocs/webgl-cap.js');
const memory = { buffer: new ArrayBuffer(16384) };
const bridge = createWebGlBridge(null, () => ({ memory }));
bridge.mount();
const imp = bridge.makeImports();
assert.ok(imp, 'bridge exposes imports');
for (const name of ['glCreateFramebuffer', 'glDeleteFramebuffer', 'glBindFramebuffer', 'glFramebufferTexture2D',
	'glFramebufferRenderbuffer', 'glCheckFramebufferStatus', 'glCreateRenderbuffer', 'glDeleteRenderbuffer',
	'glBindRenderbuffer', 'glRenderbufferStorageMultisample', 'glBlitFramebuffer',
	'glStencilFunc', 'glStencilOp', 'glStencilMask', 'glClearStencil']) {
	assert.equal(typeof imp[name], 'function', name);
}

const GL_FRAMEBUFFER = 0x8D40, GL_RENDERBUFFER = 0x8D41, COLOR0 = 0x8CE0, DEPTH = 0x8D00;
const fb = imp.glCreateFramebuffer();
const rb = imp.glCreateRenderbuffer();
const tex = imp.glCreateTexture();
assert.ok(fb > 0 && rb > 0 && tex > 0);
imp.glBindRenderbuffer(GL_RENDERBUFFER, rb);
imp.glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, 0x8058, 320, 200);
imp.glBindFramebuffer(GL_FRAMEBUFFER, fb);
imp.glFramebufferRenderbuffer(GL_FRAMEBUFFER, COLOR0, GL_RENDERBUFFER, rb);
imp.glBindTexture(0x0DE1, tex);
imp.glTexImage2D(0x0DE1, 0, 0x8CAC, 320, 200, 0, 0x1902, 0x1406, 0);
imp.glFramebufferTexture2D(GL_FRAMEBUFFER, DEPTH, 0x0DE1, tex, 0);
assert.equal(imp.glCheckFramebufferStatus(GL_FRAMEBUFFER), 0x8CD5);
imp.glBindFramebuffer(0x8CA8, fb);
imp.glBindFramebuffer(0x8CA9, 0);
imp.glBlitFramebuffer(0, 0, 320, 200, 0, 0, 320, 200, 0x4100, 0x2600);
assert.ok(log.some((e) => e[0] === 'rbStorageMS' && e[1] === 4 && e[4] === 200));
assert.ok(log.some((e) => e[0] === 'blit' && e[9] === 0x4100));
assert.ok(log.some((e) => e[0] === 'bindFramebuffer' && e[1] === 0x8CA9 && e[2] === null), 'id 0 binds the default framebuffer');
imp.glStencilFunc(0x0207, 1, 0xFFFFFFFF);
imp.glStencilOp(0x1E00, 0x1E00, 0x1E01);
imp.glStencilMask(0xFF);
imp.glClearStencil(0);
assert.ok(log.some((e) => e[0] === 'stencilFunc' && e[1] === 0x0207 && e[2] === 1 && e[3] === 0xFFFFFFFF));
assert.ok(log.some((e) => e[0] === 'stencilOp' && e[3] === 0x1E01));
console.log('PASS: webgl bridge routes framebuffer / renderbuffer / blit / stencil calls');

// Re-attachment bookkeeping: re-attaching the same slot replaces, never duplicates.
imp.glBindFramebuffer(GL_FRAMEBUFFER, fb);
imp.glFramebufferTexture2D(GL_FRAMEBUFFER, DEPTH, 0x0DE1, tex, 0);

// Context swap: every renderbuffer/framebuffer and its attachments are recreated on the new context.
log.length = 0;
canvas = canvasB;
imp.glActiveTexture(0x84C0);
const restored = log.slice();
assert.ok(restored.some((e) => e[0] === 'rbStorageMS' && e[1] === 4 && e[3] === 320), 'renderbuffer storage restored');
assert.equal(restored.filter((e) => e[0] === 'fbTex' && e[1] === DEPTH).length, 1, 'depth attachment restored exactly once');
assert.equal(restored.filter((e) => e[0] === 'fbRb' && e[1] === COLOR0).length, 1, 'color attachment restored exactly once');
console.log('PASS: render targets are rebuilt after a GL context change');

// Offscreen FBO glViewport must NOT mutate data-bud-canvas-fit canvas swapchain dimensions.
canvasB.__stele_fit = true;
canvasB.width = 1400;
canvasB.height = 836;
imp.glBindFramebuffer(GL_FRAMEBUFFER, fb);
imp.glViewport(0, 0, 128, 128);
assert.equal(canvasB.width, 1400, 'offscreen FBO viewport must not shrink canvas width');
assert.equal(canvasB.height, 836, 'offscreen FBO viewport must not shrink canvas height');
imp.glBindFramebuffer(GL_FRAMEBUFFER, 0);
imp.glViewport(0, 0, 1600, 900);
assert.equal(canvasB.width, 1600, 'default FBO 0 viewport resizes fit canvas width');
assert.equal(canvasB.height, 900, 'default FBO 0 viewport resizes fit canvas height');
console.log('PASS: offscreen FBO glViewport preserves canvas swapchain dimensions');

// Multi-mip pyramid replay and R8 single-channel byte length safety near end of WASM linear memory.
const mipTex = imp.glCreateTexture();
imp.glBindTexture(0x0DE1, mipTex);
imp.glTexImage2D(0x0DE1, 0, 0x8058, 8, 8, 0, 0x1908, 0x1401, 64);
imp.glTexImage2D(0x0DE1, 1, 0x8058, 4, 4, 0, 0x1908, 0x1401, 64);
imp.glTexSubImage2D(0x0DE1, 0, 0, 2, 8, 2, 0x1908, 0x1401, 64);
// R8 texture placed 64 bytes before the end of the 16384-byte buffer (4x16 = 64 bytes; 4*16*4 would RangeError).
const r8Tex = imp.glCreateTexture();
imp.glBindTexture(0x0DE1, r8Tex);
imp.glTexImage2D(0x0DE1, 0, 0x8229, 4, 16, 0, 0x1903, 0x1401, 16384 - 64);
log.length = 0;
canvas = canvasA;
imp.glActiveTexture(0x84C0);
const mipRestored = log.filter((e) => e[0] === 'texImage2D');
assert.ok(mipRestored.some((e) => e[2] === 0 && e[4] === 8 && e[5] === 8 && e[6] === 256), 'mip level 0 restored');
assert.ok(mipRestored.some((e) => e[2] === 1 && e[4] === 4 && e[5] === 4 && e[6] === 64), 'mip level 1 restored');
assert.ok(mipRestored.some((e) => e[3] === 0x8229 && e[4] === 4 && e[5] === 16 && e[6] === 64), 'R8 single-channel texture restored with 1-byte stride');
console.log('PASS: multi-mip pyramids, glTexSubImage2D, and R8 1-byte textures survive context transfer');

// Odd-width 49x49 GL_R16I (GL_SHORT, 4802 bytes) with GL_UNPACK_ALIGNMENT=1 followed by 4
// must replay with UNPACK_ALIGNMENT=1 (avoiding 4898-byte row-padding overflow) and restore 4.
const r16Tex = imp.glCreateTexture();
imp.glBindTexture(0x0DE1, r16Tex);
imp.glPixelStorei(0x0CF5, 1);
imp.glTexImage2D(0x0DE1, 0, 0x822E, 49, 49, 0, 0x8D94, 0x1402, 256);
imp.glPixelStorei(0x0CF5, 4);
log.length = 0;
canvas = canvasB;
imp.glActiveTexture(0x84C0);
const r16Restored = log.filter((e) => e[0] === 'texImage2D' && e[4] === 49 && e[5] === 49);
assert.equal(r16Restored.length, 1, '49x49 GL_R16I texture replayed');
assert.equal(r16Restored[0][6], 4802, '49x49 GL_R16I has exact 4802 byteLength');
assert.equal(r16Restored[0][7], 1, '49x49 GL_R16I replayed under UNPACK_ALIGNMENT=1');
assert.equal(log.filter((e) => e[0] === 'pixelStorei' && e[1] === 0x0CF5).at(-1)[2], 4, 'currentUnpackAlignment restored after replay');
console.log('PASS: odd-width 49x49 GL_R16I preserves UNPACK_ALIGNMENT=1 across context swap');

// Program compilation, shader attachment deduplication, uniform relocation, and active program restoration across context swaps.
const enc = new TextEncoder();
const writeStr = (off, s) => { const b = enc.encode(s); new Uint8Array(memory.buffer, off, b.length).set(b); return [off, b.length]; };
const vs = imp.glCreateShader(0x8B31);
const fs = imp.glCreateShader(0x8B30);
const [vsPtr, vsLen] = writeStr(64, 'void main(){}');
const [fsPtr, fsLen] = writeStr(128, 'void main(){}');
imp.glShaderSource(vs, vsPtr, vsLen);
imp.glShaderSource(fs, fsPtr, fsLen);
imp.glCompileShader(vs);
imp.glCompileShader(fs);
const prog = imp.glCreateProgram();
imp.glAttachShader(prog, vs);
imp.glAttachShader(prog, fs);
imp.glLinkProgram(prog);
assert.equal(imp.glGetProgramParameter(prog, 0x91B1), 1, 'program ready');
assert.equal(imp.glGetProgramParameter(prog, 0x8B82), 1, 'program linked');
const [uPtr, uLen] = writeStr(192, 'uMVP');
const uLoc = imp.glGetUniformLocation(prog, uPtr, uLen);
assert.ok(uLoc > 0, 'uniform location allocated');
imp.glUseProgram(prog);
imp.glDrawArraysInstanced(0x0005, 0, 4, 3);
log.length = 0;
canvas = canvasA;
imp.glDrawArraysInstanced(0x0005, 0, 4, 3);
imp.glUniform1i(uLoc, 7);
assert.ok(log.some((e) => e[0] === 'drawArraysInstanced' && e[2] === 3), 'glDrawArraysInstanced executes cleanly after context swap');
assert.ok(log.some((e) => e[0] === 'uniform1i' && e[2] === 'uMVP' && e[3] === 7), 'uniform location updated on rebuilt program');
assert.equal(log.filter((e) => e[0] === 'attachShader').length, 2, 'each shader attached exactly once on context swap');
console.log('PASS: program link, active program binding, uniform locations, and instanced draw survive context transfer');

