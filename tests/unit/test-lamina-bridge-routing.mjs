// lam_canvas_flush must paint the canvas whose size matches the buffer, so a
// page with two [data-bud-canvas] elements (stage + mini-map) keeps both.
import assert from 'node:assert/strict';

const calls = [];
function makeCanvas(id, w, h) {
	const ctx = { putImageData(img) { calls.push({ id, w: img.width, h: img.height }); } };
	return { id, width: w, height: h, getContext(kind) { return kind === '2d' ? ctx : null; } };
}
const stage = makeCanvas('stage', 640, 480);
const mini = makeCanvas('mini', 224, 224);
const canvases = [stage, mini];

globalThis.document = { querySelectorAll: () => canvases, querySelector: () => canvases[0] };
globalThis.requestAnimationFrame = () => {};
globalThis.ImageData = class { constructor(d, w, h) { this.data = d; this.width = w; this.height = h; } };

const { createLaminaBridge } = await import('../../external/liblamina/htdocs/lamina-bridge.js');
const memory = { buffer: new ArrayBuffer(640 * 480 * 4 + 16) };
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
