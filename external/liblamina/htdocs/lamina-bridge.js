/*
 * lamina-bridge.js — Browser WebAssembly bridge for liblamina 2D graphics.
 * Provides the "lamina" WASM import module.
 */

function getExport(exports, names) {
	if (!exports) return null;
	for (const name of names) {
		if (typeof exports[name] === 'function') return exports[name];
	}
	return null;
}

export function createLaminaBridge(root, getWasm) {
	let canvasCtx = null;

	return {
		mount() {
			const canvasEl = (root && root.querySelector('[data-bud-canvas]')) || document.querySelector('[data-bud-canvas]');
			if (canvasEl) {
				canvasCtx = canvasEl.getContext('2d');
				const fn = getExport(getWasm(), ['wasm_canvas_render']);
				if (fn) {
					requestAnimationFrame(() => fn());
				}
			}
		},
		makeLaminaImports() {
			return {
				lam_canvas_flush(buf_ptr, width, height) {
					if (!canvasCtx) return;
					const exports = getWasm();
					if (!exports || !exports.memory) return;
					const uptr = buf_ptr >>> 0;
					const byteLen = width * height * 4;
					if (uptr + byteLen > exports.memory.buffer.byteLength) return;
					const bytes = new Uint8ClampedArray(exports.memory.buffer, uptr, byteLen);
					const imgData = new ImageData(bytes, width, height);
					canvasCtx.putImageData(imgData, 0, 0);
				},
				lam_fill() {},
				lam_tile_draw() {},
				lam_flush() {}
			};
		},
		destroy() {
			canvasCtx = null;
		}
	};
}

if (typeof window !== 'undefined') {
	window.__wasm_capability_registry = window.__wasm_capability_registry || {};
	window.__wasm_capability_registry.lamina = (root, getWasm) => {
		const b = createLaminaBridge(root, getWasm);
		return {
			mount() { b.mount(); },
			destroy() { b.destroy(); },
			makeImports() { return b.makeLaminaImports(); }
		};
	};
}
