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
		uniform1f: (loc, v) => log.push(['uniform1f', loc ? loc.name : '', v]),
		uniform2f: (loc, x, y) => log.push(['uniform2f', loc ? loc.name : '', x, y]),
		uniform3f: (loc, x, y, z) => log.push(['uniform3f', loc ? loc.name : '', x, y, z]),
		uniform4f: (loc, x, y, z, w) => log.push(['uniform4f', loc ? loc.name : '', x, y, z, w]),
		uniform2fv: (loc, arr) => log.push(['uniform2fv', loc ? loc.name : '', arr.length]),
		uniform3fv: (loc, arr) => log.push(['uniform3fv', loc ? loc.name : '', arr.length]),
		uniformMatrix4fv: (loc, trans, arr) => log.push(['uniformMatrix4fv', loc ? loc.name : '', !!trans, arr.length, arr[0]]),
		getUniformBlockIndex: (p, name) => linked.has(p) && name === 'NdFrameUBO' ? 0 : 0xFFFFFFFF,
		uniformBlockBinding: (p, bidx, bind) => log.push(['uniformBlockBinding', p ? p.n : 0, bidx, bind]),
		bindBufferBase: (t, idx, b) => log.push(['bindBufferBase', t, idx, b ? b.n : null]),
		drawArrays: (m, f, c) => log.push(['drawArrays', m, f, c]),
		drawElements: (m, c, t, o) => log.push(['drawElements', m, c, t, o]),
		drawElementsInstanced: (m, c, t, o, ic) => log.push(['drawElementsInstanced', m, c, t, o, ic]),
		drawArraysInstanced: (m, f, c, ic) => {
			if (!activeProgram || !linked.has(activeProgram)) throw new Error('drawArraysInstanced: current program is not linked');
			log.push(['drawArraysInstanced', activeProgram.n, ic]);
		},
		enableVertexAttribArray: (i) => log.push(['enableVAA', i]),
		disableVertexAttribArray: (i) => log.push(['disableVAA', i]),
		vertexAttribPointer: (i, s, t, n, st, o) => log.push(['vap', i, s, t, !!n, st, o]),
		vertexAttribDivisor: (i, d) => log.push(['vad', i, d]),
		enable: (cap) => log.push(['enable', cap]),
		blendFunc: (sf, df) => log.push(['blendFunc', sf, df]),
		bindBuffer: (t, b) => log.push(['bindBuffer', t, b ? b.n : null]),
		bufferData: (t, d, u) => log.push(['bufferData', t, typeof d === 'number' ? d : d.byteLength, u]),
		bufferSubData: (t, off, view, srcOff, len) => {
			const n = (len !== undefined) ? len : view.byteLength;
			log.push(['bufferSubData', t, off, n]);
		},
		pixelStorei: (p, v) => { if (p === 0x0CF5) unpackAlignment = v; log.push(['pixelStorei', p, v]); },
		deleteFramebuffer: (f) => log.push(['deleteFramebuffer', f.n]),
		deleteRenderbuffer: (r) => log.push(['deleteRenderbuffer', r.n]),
		bindFramebuffer: (t, f) => log.push(['bindFramebuffer', t, f ? f.n : null]),
		bindRenderbuffer: (t, r) => log.push(['bindRenderbuffer', t, r ? r.n : null]),
		bindTexture: (t, tex) => log.push(['bindTexture', t, tex ? tex.n : null]),
		texParameteri: (t, p, v) => log.push(['texParameteri', t, p, v]),
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
		disable: (cap) => log.push(['disable', cap]), colorMask: () => {}, depthMask: (f) => log.push(['depthMask', !!f]),
		clearColor: () => {}, clearDepth: () => {},
		clear: (mask) => log.push(['clear', mask]),
		getExtension: () => null,
		activeTexture: (u) => log.push(['activeTexture', u]),
	};
	return gl;
}

const log = [];
const glA = makeGl(log);
const glB = makeGl(log);
let current = glA;
let canvasA = { isConnected: true, getContext: () => glA };
let canvasB = { isConnected: false, getContext: () => glB };
let canvas = canvasA;
const swapCanvas = (next) => { canvas.isConnected = false; next.isConnected = true; canvas = next; };
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
swapCanvas(canvasB);
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
swapCanvas(canvasA);
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
swapCanvas(canvasB);
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
swapCanvas(canvasA);
imp.glDrawArraysInstanced(0x0005, 0, 4, 3);
imp.glUniform1i(uLoc, 7);
assert.ok(log.some((e) => e[0] === 'drawArraysInstanced' && e[2] === 3), 'glDrawArraysInstanced executes cleanly after context swap');
assert.ok(log.some((e) => e[0] === 'uniform1i' && e[2] === 'uMVP' && e[3] === 7), 'uniform location updated on rebuilt program');
assert.equal(log.filter((e) => e[0] === 'attachShader').length, 2, 'each shader attached exactly once on context swap');
console.log('PASS: program link, active program binding, uniform locations, and instanced draw survive context transfer');

// FBO-backed null-pixel textures are cleared during rebuildContextResources (prevents WebGL LAZY_INIT).
assert.ok(restored.some((e) => e[0] === 'clear' && (e[1] & 0x4100) === 0x4100), 'FBO attached to null texture is cleared on context swap');
console.log('PASS: null-pixel FBO attachments are cleared on context rebuild');

// Per-unit glActiveTexture + glBindTexture isolation across unbinds on other units and across context swaps.
const tU0 = imp.glCreateTexture();
const tU1 = imp.glCreateTexture();
imp.glActiveTexture(0x84C0);
imp.glBindTexture(0x0DE1, tU0);
imp.glTexImage2D(0x0DE1, 0, 0x8058, 4, 4, 0, 0x1908, 0x1401, 64);
imp.glActiveTexture(0x84C1);
imp.glBindTexture(0x0DE1, tU1);
imp.glTexImage2D(0x0DE1, 0, 0x8229, 8, 8, 0, 0x1903, 0x1401, 64);
imp.glActiveTexture(0x84CC); // TEXTURE12
imp.glBindTexture(0x0DE1, 0); // Unbinding TEXTURE12 must NOT clobber TEXTURE0 or TEXTURE1
imp.glActiveTexture(0x84C0);
imp.glTexParameteri(0x0DE1, 0x2801, 0x2600);
imp.glActiveTexture(0x84C1);
imp.glTexParameteri(0x0DE1, 0x2801, 0x2601);
log.length = 0;
swapCanvas(canvasB);
imp.glViewport(0, 0, 800, 600);
assert.ok(log.some((e) => e[0] === 'texImage2D' && e[4] === 4 && e[5] === 4), 'unit 0 texture metadata preserved across unit 12 unbind');
assert.ok(log.some((e) => e[0] === 'texImage2D' && e[4] === 8 && e[5] === 8), 'unit 1 texture metadata preserved across unit 12 unbind');
const postBindU0 = log.findIndex((e, i) => e[0] === 'activeTexture' && e[1] === 0x84C0 && log[i + 1] && log[i + 1][0] === 'bindTexture' && log[i + 1][2] !== null);
const postBindU1 = log.findIndex((e, i) => e[0] === 'activeTexture' && e[1] === 0x84C1 && log[i + 1] && log[i + 1][0] === 'bindTexture' && log[i + 1][2] !== null);
assert.ok(postBindU0 >= 0 && postBindU1 >= 0, 'per-unit TEXTURE0 and TEXTURE1 bindings restored on context swap');
assert.equal(log.filter((e) => e[0] === 'activeTexture').at(-1)[1], 0x84C1, 'currentActiveTextureUnit restored after rebuild');
console.log('PASS: per-unit texture bindings survive unrelated unit unbinds and context swaps');

// glCreateTexture returns 0 when underlying c.createTexture() returns null (never poisons slots).
const origCreateTexB = glB.createTexture;
glB.createTexture = () => null;
assert.equal(imp.glCreateTexture(), 0, 'glCreateTexture returns 0 on null allocation');
glB.createTexture = origCreateTexB;
console.log('PASS: glCreateTexture returns 0 when underlying context returns null');

// DOM mode findCanvas() must NEVER hijack a plain [data-bud-canvas] (e.g. #room-map-canvas in non-Seven area).
const prevDoc = globalThis.document;
let mapCtxRequested = false;
const fakeMapCanvas = { isConnected: true, getContext: () => { mapCtxRequested = true; return makeGl([]); } };
globalThis.document = {
	querySelector: (sel) => (sel === '[data-bud-canvas]' || sel === 'canvas') ? fakeMapCanvas : null,
	querySelectorAll: () => [fakeMapCanvas],
};
const nonSevenBridge = createWebGlBridge(null, () => ({ memory }));
const nonSevenImp = nonSevenBridge.makeImports();
assert.equal(nonSevenImp.glCreateTexture(), 0, 'DOM bridge stays unbound when only [data-bud-canvas] exists');
assert.equal(mapCtxRequested, false, 'DOM bridge must never request WebGL context on plain [data-bud-canvas]');
globalThis.document = prevDoc;
console.log('PASS: DOM WebGL bridge never hijacks plain [data-bud-canvas] in non-Seven areas');

// Optional WebGL bridge profiler counters & P1 fast-path getGl() (0 DOM queries steady-state).
assert.equal(bridge.perf.isEnabled(), false, 'bridge perf profiler disabled by default');
bridge.perf.reset();
imp.glUniform1i(uLoc, 1);
assert.equal(bridge.perf.snapshot().uniformUploads, 0, 'disabled profiler records 0 uniformUploads');
bridge.perf.enable(true);
bridge.perf.reset();
imp.glGetUniformLocation(prog, uPtr, uLen);
imp.glUniform1i(uLoc, 2);
imp.glUniformMatrix4fv(uLoc, 1, 0, 256);
imp.glBufferData(0x8892, 64, 16, 0x88E8);
imp.glBufferSubData(0x8892, 4, 8, 64);
imp.glDrawArraysInstanced(0x0005, 0, 4, 2);
imp.glBlitFramebuffer(0, 0, 10, 10, 0, 0, 10, 10, 0x4000, 0x2600);
const snap = bridge.perf.snapshot();
assert.equal(snap.enabled, true, 'profiler reports enabled');
assert.equal(snap.getUniformLocationCalls, 1, 'counts glGetUniformLocation calls');
assert.equal(snap.uniformUploads, 2, 'counts uniform uploads (1i + Matrix4fv)');
assert.equal(snap.bufferUploads, 2, 'counts buffer uploads (glBufferData + glBufferSubData)');
assert.equal(snap.draws, 1, 'counts draw calls');
assert.equal(snap.blits, 1, 'counts framebuffer blits');
assert.ok(snap.getGlCalls >= 7, 'counts getGl calls');
assert.equal(snap.domQueries, 0, 'steady-state getGl() performs 0 DOM queries');
assert.ok(snap.uniformLocsLen >= 2, 'reports uniformLocs table length');
assert.ok(log.some((e) => e[0] === 'uniformMatrix4fv' && e[3] === 16), 'glUniformMatrix4fv forwards 16 floats via cached view');
assert.ok(log.some((e) => e[0] === 'bufferSubData' && e[1] === 0x8892 && e[2] === 4 && e[3] === 8), 'glBufferSubData forwards exact offset and byte length');
// bridge.mount() forces a canvas re-check even if the old canvas remains connected.
canvasA.isConnected = true;
canvas = canvasA;
bridge.mount();
assert.equal(bridge.perf.snapshot().domQueries, 1, 'mount() triggers exactly 1 DOM query to detect canvas replacement');
bridge.perf.enable(false);
console.log('PASS: WebGL bridge fast-path getGl() (0 DOM queries steady-state) & profiler counters verified');

// Phase 4C.1: WebGL2 UBO binding (glBindBufferBase / glGetUniformBlockIndex / glUniformBlockBinding) + context-swap restore.
const GL_UNIFORM_BUFFER = 0x8A11;
const uboBuf = imp.glCreateBuffer();
assert.ok(uboBuf > 0);
imp.glBindBuffer(GL_UNIFORM_BUFFER, uboBuf);
imp.glBufferData(GL_UNIFORM_BUFFER, 0, 256, 0x88E8);
imp.glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboBuf);
imp.glBufferSubData(GL_UNIFORM_BUFFER, 0, 64, 128);
const uboNameBytes = new TextEncoder().encode('NdFrameUBO');
new Uint8Array(memory.buffer, 512, uboNameBytes.length).set(uboNameBytes);
const bidx = imp.glGetUniformBlockIndex(prog, 512, uboNameBytes.length);
assert.equal(bidx, 0, 'glGetUniformBlockIndex resolves NdFrameUBO to index 0');
imp.glUniformBlockBinding(prog, bidx, 0);
assert.ok(log.some((e) => e[0] === 'bindBufferBase' && e[1] === GL_UNIFORM_BUFFER && e[2] === 0 && e[3] !== null), 'glBindBufferBase forwarded');
assert.ok(log.some((e) => e[0] === 'uniformBlockBinding' && e[2] === 0 && e[3] === 0), 'glUniformBlockBinding forwarded');
assert.ok(log.some((e) => e[0] === 'bufferSubData' && e[1] === GL_UNIFORM_BUFFER && e[2] === 0 && e[3] === 64), 'glBufferSubData on GL_UNIFORM_BUFFER forwarded');
// Context swap must restore UBO buffer base and program block binding.
log.length = 0;
swapCanvas(canvasB);
imp.glActiveTexture(0x84C0);
assert.ok(log.some((e) => e[0] === 'bindBufferBase' && e[1] === GL_UNIFORM_BUFFER && e[2] === 0 && e[3] !== null), 'UBO base binding restored across context swap');
assert.ok(log.some((e) => e[0] === 'uniformBlockBinding' && e[2] === 0 && e[3] === 0), 'UBO program block binding restored across context swap');
console.log('PASS: WebGL2 UBO binding and context-swap restoration verified');

// Phase 4C.2: Flat uint32 command ring buffer (glCmdFlush) decoding, float/mat4 bit accuracy, and perf counters.
bridge.perf.enable(true);
bridge.perf.reset();
log.length = 0;
const cmdBase = 2048;
const u32 = new Uint32Array(memory.buffer, cmdBase, 128);
const f32 = new Float32Array(memory.buffer, cmdBase, 128);
let w = 0;
u32[w++] = 1; u32[w++] = prog;                         // USE_PROGRAM
u32[w++] = 2; u32[w++] = 0x84C0;                       // ACTIVE_TEXTURE TEXTURE0
u32[w++] = 3; u32[w++] = 0x0DE1; u32[w++] = tex;       // BIND_TEXTURE
u32[w++] = 10; u32[w++] = uLoc; f32[w++] = 3.25;       // UNIFORM1F
u32[w++] = 13; u32[w++] = uLoc; f32[w++] = 1.0; f32[w++] = 2.0; f32[w++] = 3.0; f32[w++] = 4.0; // UNIFORM4F
u32[w++] = 14; u32[w++] = uLoc; u32[w++] = 0;          // UNIFORM_MAT4
for (let k = 0; k < 16; k++) f32[w++] = (k === 0) ? 7.5 : 0.0;
u32[w++] = 16; u32[w++] = 0x0004; u32[w++] = 600; u32[w++] = 0x1403; u32[w++] = 0; // DRAW_ELEMENTS
u32[w++] = 17; u32[w++] = 0x0004; u32[w++] = 36; u32[w++] = 0x1403; u32[w++] = 0; u32[w++] = 128; // DRAW_ELEMENTS_INSTANCED
imp.glCmdFlush(cmdBase, w);
const csnap = bridge.perf.snapshot();
assert.equal(csnap.cmdFlushes, 1, 'exactly 1 FFI flush recorded');
assert.equal(csnap.cmdOps, 8, '8 batched ops decoded in a single flush');
assert.equal(csnap.getGlCalls, 1, 'single getGl() per command flush');
assert.equal(csnap.uniformUploads, 3, '3 uniform ops decoded');
assert.equal(csnap.draws, 2, '2 draw calls decoded');
assert.ok(log.some((e) => e[0] === 'uniform1f' && e[2] === 3.25), 'float bit-packing preserved');
assert.ok(log.some((e) => e[0] === 'uniform4f' && e[2] === 1 && e[5] === 4), 'vec4 bit-packing preserved');
assert.ok(log.some((e) => e[0] === 'uniformMatrix4fv' && e[3] === 16 && e[4] === 7.5), 'mat4 bit-packing preserved');
assert.ok(log.some((e) => e[0] === 'drawElements' && e[2] === 600), 'drawElements decoded');
assert.ok(log.some((e) => e[0] === 'drawElementsInstanced' && e[5] === 128), 'drawElementsInstanced decoded');
bridge.perf.enable(false);
console.log('PASS: flat uint32 glCmdFlush command ring decoding & single-FFI batching verified');

// GPU pass timer queries (EXT_disjoint_timer_query_webgl2) + query pool reuse + disjoint rejection.
{
	const tLog = [];
	const tGl = makeGl(tLog);
	const TIME_ELAPSED_EXT = 0x88BF;
	const GPU_DISJOINT_EXT = 0x8FBB;
	let qSeq = 0;
	let disjointFlag = false;
	const qTimes = new Map();
	tGl.QUERY_RESULT_AVAILABLE = 0x8867;
	tGl.QUERY_RESULT = 0x8866;
	tGl.getExtension = (name) => name === 'EXT_disjoint_timer_query_webgl2'
		? { TIME_ELAPSED_EXT, GPU_DISJOINT_EXT }
		: null;
	tGl.getParameter = (p) => p === GPU_DISJOINT_EXT ? disjointFlag : 0;
	tGl.createQuery = () => ({ kind: 'query', n: ++qSeq });
	tGl.deleteQuery = (q) => tLog.push(['deleteQuery', q.n]);
	tGl.beginQuery = (target, q) => {
		assert.equal(target, TIME_ELAPSED_EXT);
		qTimes.set(q, 2_500_000); // 2.5 ms in ns
		tLog.push(['beginQuery', q.n]);
	};
	tGl.endQuery = (target) => {
		assert.equal(target, TIME_ELAPSED_EXT);
		tLog.push(['endQuery']);
	};
	tGl.getQueryParameter = (q, pname) => {
		if (pname === tGl.QUERY_RESULT_AVAILABLE) return true;
		if (pname === tGl.QUERY_RESULT) return qTimes.get(q) || 0;
		return 0;
	};
	const tCanvas = { isConnected: true, getContext: () => tGl };
	swapCanvas(tCanvas);
	bridge.mount();
	// Disabled by default -> no-op.
	bridge.perf.enable(false);
	imp.glTimerBegin(2);
	imp.glTimerEnd(2);
	assert.equal(qSeq, 0, 'disabled profiler allocates 0 timer queries');
	// Enabled -> records 2.5ms for pass 2 and reuses query from pool on second call.
	bridge.perf.enable(true);
	bridge.perf.reset();
	imp.glTimerBegin(2);
	imp.glTimerEnd(2);
	imp.glTimerBegin(2);
	imp.glTimerEnd(2);
	const ts1 = bridge.perf.snapshot();
	assert.equal(ts1.gpuTimerSupported, true, 'reports EXT_disjoint_timer_query_webgl2 support');
	assert.equal(ts1.gpuTimerDisjoints, 0, 'no disjoints on clean run');
	assert.equal(ts1.gpuPasses[2].samples, 2, 'recorded 2 samples for pass 2');
	assert.ok(Math.abs(ts1.gpuPasses[2].avgMs - 2.5) < 1e-6, 'avgMs matches 2.5ms');
	assert.equal(qSeq, 1, 'reused pooled WebGLQuery instead of allocating a second one');
	// Disjoint event discards sample and increments gpuTimerDisjoints.
	disjointFlag = true;
	imp.glTimerBegin(2);
	imp.glTimerEnd(2);
	const ts2 = bridge.perf.snapshot();
	assert.equal(ts2.gpuTimerDisjoints, 1, 'disjoint event counted');
	assert.equal(ts2.gpuPasses[2].samples, 2, 'disjoint sample discarded');
	bridge.perf.enable(false);
	// GL_TEXTURE_2D_ARRAY binding does not clobber GL_TEXTURE_2D binding on unit 0.
	const tex2d = imp.glCreateTexture();
	const texArr = imp.glCreateTexture();
	imp.glActiveTexture(0x84C0);
	imp.glBindTexture(0x0DE1, tex2d);
	imp.glBindTexture(0x8C1A, texArr);
	imp.glTexImage2D(0x0DE1, 0, 0x8058, 2, 2, 0, 0x1908, 0x1401, 64);
	// Re-upload same dimensions should succeed with zero errors (in-place mirror reuse).
	imp.glTexImage2D(0x0DE1, 0, 0x8058, 2, 2, 0, 0x1908, 0x1401, 64);
	console.log('PASS: GPU pass timer queries (EXT_disjoint_timer_query_webgl2) and GL_TEXTURE_2D_ARRAY isolation verified');
}
