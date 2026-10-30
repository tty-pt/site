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

const { createLaminaBridge } = await import('../../external/liblamina/htdocs/lamina-bridge.js');
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
