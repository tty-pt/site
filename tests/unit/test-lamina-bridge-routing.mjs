// lam_canvas_flush must paint the canvas whose size matches the buffer, so a
// page with two [data-bud-canvas] elements (stage + mini-map) keeps both.
import assert from 'node:assert/strict';

const calls = [];
function makeCanvas(id, w, h, fit = false) {
	const ctx = { putImageData(img) { calls.push({ id, w: img.width, h: img.height }); } };
	return {
		id, width: w, height: h,
		hasAttribute(name) { return fit && name === 'data-bud-canvas-fit'; },
		getContext(kind) { return kind === '2d' ? ctx : null; }
	};
}
const stage = makeCanvas('stage', 640, 480);
const mini = makeCanvas('mini', 224, 224);
const canvases = [stage, mini];

globalThis.document = { querySelectorAll: () => canvases, querySelector: () => canvases[0] };
globalThis.requestAnimationFrame = () => {};
globalThis.ImageData = class { constructor(d, w, h) { this.data = d; this.width = w; this.height = h; } };

const { createLaminaBridge, SHADERS } = await import('../../external/liblamina/htdocs/lamina-cap.js');
const memory = { buffer: new ArrayBuffer(1920 * 1080 * 4 + 16) };
const bridge = createLaminaBridge(null, () => ({ memory }));
bridge.mount();
const { lam_canvas_flush } = bridge.makeLaminaImports();

lam_canvas_flush(0, 640, 480);
lam_canvas_flush(0, 224, 224);
lam_canvas_flush(0, 640, 480);
assert.deepEqual(calls.map((c) => c.id), ['stage', 'mini', 'stage']);

// A buffer matching no canvas falls back to the first one.
lam_canvas_flush(0, 100, 100);
assert.equal(calls.at(-1).id, 'stage');

console.log('PASS: lamina bridge routes each flush to the canvas of matching size');

// A canvas marked data-bud-canvas-fit follows the frame size: the full-screen
// stage is resized to the flushed w x h, the fixed-size mini-map keeps its own.
calls.length = 0;
const fitStage = makeCanvas('fit-stage', 640, 480, true);
const fitMini = makeCanvas('fit-mini', 224, 224);
canvases.splice(0, canvases.length, fitStage, fitMini);
const bridge2 = createLaminaBridge(null, () => ({ memory }));
bridge2.mount();
const flush2 = bridge2.makeLaminaImports().lam_canvas_flush;
flush2(0, 1456, 836);
assert.equal(calls.at(-1).id, 'fit-stage');
assert.deepEqual([fitStage.width, fitStage.height], [1456, 836]);
assert.deepEqual([calls.at(-1).w, calls.at(-1).h], [1456, 836]);
flush2(0, 224, 224);
assert.equal(calls.at(-1).id, 'fit-mini');
flush2(0, 1456, 836);
assert.equal(calls.at(-1).id, 'fit-stage');           // exact match now, no resize
flush2(0, 1920, 1080);
assert.deepEqual([fitStage.width, fitStage.height], [1920, 1080]);
assert.deepEqual([fitMini.width, fitMini.height], [224, 224]);

console.log('PASS: a fit canvas is resized to the flushed frame; fixed canvases are untouched');

// Verify 3D scene and shader logic has been migrated out of JS to pure C (nd_seven_gl.c)
assert.equal(SHADERS, undefined, '3D shaders must live in pure C (mods/nd/ux/nd_seven_gl.c), not lamina-bridge.js');

console.log('PASS: lamina-bridge.js is purely 2D canvas pass-through with zero 3D shaders in JS');

// Verify generic lam_image_request / lam_image_read exports exist and return 0 while pending
const { lam_image_request, lam_image_read } = bridge.makeLaminaImports();
assert.equal(typeof lam_image_request, 'function');
assert.equal(typeof lam_image_read, 'function');
assert.equal(lam_image_request(0, 0), 0);
assert.equal(lam_image_read(0, 0, 0, 0, 0, 0), 0);
console.log('PASS: generic lam_image_request and lam_image_read imports verified');

// Verify webgl-bridge.js offloads shader compilation to a separate Worker while glGetProgramParameter(GL_COMPLETION_STATUS_KHR) stays 0 until worker completes
{
	const { createWebGlBridge } = await import('../../external/liblamina/htdocs/webgl-bridge.js');
	let localCompileCalls = 0;
	let localLinkCalls = 0;
	let workerMessages = [];
	let activeWorker = null;

	const mockGl = {
		VERTEX_SHADER: 0x8B31,
		FRAGMENT_SHADER: 0x8B30,
		COMPILE_STATUS: 0x8B81,
		LINK_STATUS: 0x8B82,
		getExtension(name) { return null; },
		createShader(type) { return { type }; },
		shaderSource() {},
		compileShader() { localCompileCalls++; },
		getShaderParameter() { return true; },
		createProgram() { return { prog: true }; },
		attachShader() {},
		bindAttribLocation() {},
		linkProgram() { localLinkCalls++; },
		getProgramParameter() { return true; },
		deleteShader() {},
		deleteProgram() {}
	};
	const mockGlCanvas = {
		width: 640,
		height: 480,
		hasAttribute(name) { return name === 'data-bud-webgl'; },
		getContext(kind) { return kind === 'webgl2' ? mockGl : null; }
	};

	globalThis.OffscreenCanvas = class { constructor(w, h) { this.width = w; this.height = h; } };
	globalThis.Blob = class { constructor(parts) { this.parts = parts; } };
	globalThis.URL.createObjectURL = () => 'blob:mock-shader-worker';
	globalThis.URL.revokeObjectURL = () => {};
	globalThis.Worker = class {
		constructor(url) {
			this.url = url;
			this.onmessage = null;
			this.onerror = null;
			activeWorker = this;
		}
		postMessage(msg) {
			workerMessages.push(msg);
		}
		terminate() {}
	};

	const wglMem = { buffer: new ArrayBuffer(1024) };
	const enc = new TextEncoder();
	const vsBytes = enc.encode('#version 300 es\nvoid main(){gl_Position=vec4(0.0);}');
	const fsBytes = enc.encode('#version 300 es\nprecision highp float;out vec4 c;void main(){c=vec4(1.0);}');
	new Uint8Array(wglMem.buffer, 16, vsBytes.length).set(vsBytes);
	new Uint8Array(wglMem.buffer, 128, fsBytes.length).set(fsBytes);

	const wglRoot = { querySelector: () => mockGlCanvas };
	const wglBridge = createWebGlBridge(wglRoot, () => ({ memory: wglMem }));
	wglBridge.mount();
	const glImp = wglBridge.makeImports();

	const vsId = glImp.glCreateShader(0x8B31);
	const fsId = glImp.glCreateShader(0x8B30);
	glImp.glShaderSource(vsId, 16, vsBytes.length);
	glImp.glCompileShader(vsId);
	glImp.glShaderSource(fsId, 128, fsBytes.length);
	glImp.glCompileShader(fsId);

	const progId = glImp.glCreateProgram();
	glImp.glAttachShader(progId, vsId);
	glImp.glAttachShader(progId, fsId);
	glImp.glLinkProgram(progId);
	glImp.glDeleteShader(vsId);
	glImp.glDeleteShader(fsId);

	// Local GL must NOT have compiled or linked yet — work was offloaded to the shader worker!
	assert.equal(localCompileCalls, 0, 'glCompileShader must be offloaded to the shader worker');
	assert.equal(localLinkCalls, 0, 'glLinkProgram must be offloaded to the shader worker');
	assert.equal(workerMessages.length, 1, 'shader worker must receive webgl:compile message');
	assert.equal(workerMessages[0].type, 'webgl:compile');
	assert.equal(workerMessages[0].programId, progId);

	// While worker is compiling, GL_COMPLETION_STATUS_KHR (0x91B1) must return 0
	assert.equal(glImp.glGetProgramParameter(progId, 0x91B1), 0);
	assert.equal(glImp.glGetProgramParameter(progId, 0x91B1), 0);
	assert.equal(localLinkCalls, 0);

	// Simulate shader worker finishing background compilation
	activeWorker.onmessage({ data: { type: 'webgl:compiled', programId: progId, rev: workerMessages[0].rev, ok: true } });

	// Now GL_COMPLETION_STATUS_KHR returns 1 and local program is linked from warm driver cache
	assert.equal(glImp.glGetProgramParameter(progId, 0x91B1), 1);
	assert.equal(glImp.glGetProgramParameter(progId, 0x8B82), 1);
	assert.equal(localCompileCalls, 2);
	assert.equal(localLinkCalls, 1);

	wglBridge.destroy();
	console.log('PASS: webgl-bridge.js offloads shader compilation to background Worker and reports completion via GL_COMPLETION_STATUS_KHR');
}


