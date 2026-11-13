// WebGL bridge offscreen render-target API: framebuffer / renderbuffer / blit routing,
// attachment bookkeeping, and rebuild when the GL context changes (canvas swap).
import assert from 'node:assert/strict';

function makeGl(log) {
	let id = 0;
	const mk = (kind) => ({ kind, n: ++id });
	const gl = {
		FRAMEBUFFER: 0x8D40, RENDERBUFFER: 0x8D41,
		createFramebuffer: () => mk('fb'), createRenderbuffer: () => mk('rb'), createTexture: () => mk('tex'),
		deleteFramebuffer: (f) => log.push(['deleteFramebuffer', f.n]),
		deleteRenderbuffer: (r) => log.push(['deleteRenderbuffer', r.n]),
		bindFramebuffer: (t, f) => log.push(['bindFramebuffer', t, f ? f.n : null]),
		bindRenderbuffer: (t, r) => log.push(['bindRenderbuffer', t, r ? r.n : null]),
		bindTexture: () => {}, texParameteri: () => {}, texImage2D: (...a) => log.push(['texImage2D', a[0], a[2], a[3], a[4]]),
		renderbufferStorageMultisample: (t, s, f, w, h) => log.push(['rbStorageMS', s, f, w, h]),
		framebufferRenderbuffer: (t, a, rt, r) => log.push(['fbRb', a, r ? r.n : null]),
		framebufferTexture2D: (t, a, tt, tex, l) => log.push(['fbTex', a, tex ? tex.n : null, l]),
		checkFramebufferStatus: () => 0x8CD5,
		blitFramebuffer: (...a) => log.push(['blit', ...a]),
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
const memory = { buffer: new ArrayBuffer(1024) };
const bridge = createWebGlBridge(null, () => ({ memory }));
bridge.mount();
const imp = bridge.makeImports();
assert.ok(imp, 'bridge exposes imports');
for (const name of ['glCreateFramebuffer', 'glDeleteFramebuffer', 'glBindFramebuffer', 'glFramebufferTexture2D',
	'glFramebufferRenderbuffer', 'glCheckFramebufferStatus', 'glCreateRenderbuffer', 'glDeleteRenderbuffer',
	'glBindRenderbuffer', 'glRenderbufferStorageMultisample', 'glBlitFramebuffer']) {
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
console.log('PASS: webgl bridge routes framebuffer / renderbuffer / blit calls');

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
