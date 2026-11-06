/*
 * lamina-bridge.js — Browser WebAssembly bridge for liblamina 2D graphics.
 * Provides the "lamina" WASM import module.
 *
 * Automatically mounts onto the active [data-bud-canvas].
 * When WebGL is supported by the canvas, frames flushed via lam_canvas_flush
 * are uploaded as a texture and rendered as a hardware-accelerated textured quad.
 * If WebGL is unavailable or unsupported, falls back cleanly to 2D putImageData.
 */

function getExport(exports, names) {
	if (!exports) return null;
	for (const name of names) {
		if (typeof exports[name] === 'function') return exports[name];
	}
	return null;
}

const VS_QUAD = `attribute vec2 a_pos;
attribute vec2 a_uv;
varying vec2 v_uv;
void main() {
	v_uv = a_uv;
	gl_Position = vec4(a_pos, 0.0, 1.0);
}`;

const FS_QUAD = `precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_time;
uniform vec2 u_res;
void main() {
	vec4 base = texture2D(u_tex, v_uv);
	bool is_water = (base.b > base.r * 1.25 && base.b > 0.30 && base.g > base.r * 0.9);
	if (is_water && u_time > 0.0) {
		vec2 p = v_uv * u_res * 0.5;
		float t = u_time * 2.0;
		float w1 = sin(p.x * 0.12 + p.y * 0.06 + t);
		float w2 = cos(p.x * 0.09 - p.y * 0.08 + t * 0.8);
		float caustic = w1 * 0.5 + w2 * 0.5;
		if (caustic > 0.35) {
			float highlight = (caustic - 0.35) * 0.25;
			base.rgb += vec3(highlight * 0.8, highlight * 0.95, highlight);
		} else if (caustic < -0.35) {
			float shadow = (-caustic - 0.35) * 0.20;
			base.rgb -= vec3(shadow * 0.5, shadow * 0.3, shadow * 0.1);
		}
	}
	gl_FragColor = base;
}`;

function initGlPipeline(gl) {
	try {
		const vs = gl.createShader(gl.VERTEX_SHADER);
		gl.shaderSource(vs, VS_QUAD);
		gl.compileShader(vs);

		const fs = gl.createShader(gl.FRAGMENT_SHADER);
		gl.shaderSource(fs, FS_QUAD);
		gl.compileShader(fs);

		const prog = gl.createProgram();
		gl.attachShader(prog, vs);
		gl.attachShader(prog, fs);
		gl.linkProgram(prog);

		if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
			return null;
		}

		gl.useProgram(prog);

		// Quad vertices: 2 triangles (x, y, u, v)
		const verts = new Float32Array([
			-1.0, -1.0,  0.0,  1.0,
			 1.0, -1.0,  1.0,  1.0,
			-1.0,  1.0,  0.0,  0.0,
			-1.0,  1.0,  0.0,  0.0,
			 1.0, -1.0,  1.0,  1.0,
			 1.0,  1.0,  1.0,  0.0
		]);

		const vbo = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
		gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

		const aPos = gl.getAttribLocation(prog, 'a_pos');
		const aUv = gl.getAttribLocation(prog, 'a_uv');
		gl.enableVertexAttribArray(aPos);
		gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 16, 0);
		gl.enableVertexAttribArray(aUv);
		gl.vertexAttribPointer(aUv, 2, gl.FLOAT, false, 16, 8);

		const tex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, tex);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

		const uTime = gl.getUniformLocation(prog, 'u_time');
		const uRes = gl.getUniformLocation(prog, 'u_res');
		return { prog, tex, vbo, uTime, uRes };
	} catch (e) {
		return null;
	}
}

export function createLaminaBridge(root, getWasm) {
	// One rendering target per <canvas data-bud-canvas>. A page may carry
	// several (e.g. a game stage and a mini-map); each lam_canvas_flush(buf, w,
	// h) is routed to the canvas whose intrinsic size is exactly w x h, falling
	// back to the first one, so independent frame producers never overwrite
	// each other's canvas.
	const targets = new WeakMap();
	const kicked = new WeakSet();

	function allCanvases() {
		const scope = root && root.querySelectorAll ? root : document;
		let list = scope.querySelectorAll('[data-bud-canvas]');
		if (!list.length && scope !== document) list = document.querySelectorAll('[data-bud-canvas]');
		return list;
	}

	function pickCanvas(width, height) {
		// 1. a canvas whose intrinsic size is exactly w x h (fixed-size canvases
		//    such as a mini-map keep their own frames);
		// 2. a canvas marked data-bud-canvas-fit: it follows the frame size, so
		//    it is resized to w x h and painted 1:1;
		// 3. the first canvas.
		const list = allCanvases();
		let first = null, fit = null;
		for (const c of list) {
			if (!first) first = c;
			if (c.width === width && c.height === height) return c;
			if (!fit && c.hasAttribute && c.hasAttribute('data-bud-canvas-fit')) fit = c;
		}
		if (fit) {
			fit.width = width;
			fit.height = height;
			return fit;
		}
		return first;
	}

	function targetFor(el) {
		let t = targets.get(el);
		if (t) return t;
		t = { gl: null, glPipe: null, canvas2d: null };
		// Try WebGL / WebGL2 context first with preserveDrawingBuffer enabled
		try {
			const glOpts = { preserveDrawingBuffer: true, alpha: true, antialias: true };
			t.gl = el.getContext('webgl2', glOpts) || el.getContext('webgl', glOpts);
			if (t.gl) t.glPipe = initGlPipeline(t.gl);
		} catch (e) {
			t.gl = null;
			t.glPipe = null;
		}
		if (!t.gl || !t.glPipe) {
			t.gl = null;
			t.glPipe = null;
			try {
				t.canvas2d = el.getContext('2d');
			} catch (e) {
				t.canvas2d = null;
			}
		}
		targets.set(el, t);
		return t;
	}

	function kickRender() {
		const fn = getExport(getWasm(), ['wasm_canvas_render']);
		if (fn) requestAnimationFrame(() => fn());
	}

	function bindAll() {
		for (const c of allCanvases()) {
			targetFor(c);
			if (!kicked.has(c)) {
				kicked.add(c);
				kickRender();
			}
		}
	}

	return {
		mount() {
			bindAll();
		},
		makeLaminaImports() {
			return {
				lam_canvas_flush(buf_ptr, width, height) {
					const canvasEl = pickCanvas(width, height);
					if (!canvasEl) return;
					const t = targetFor(canvasEl);
					if (!kicked.has(canvasEl)) {
						kicked.add(canvasEl);
						kickRender();
					}
					const exports = getWasm();
					if (!exports || !exports.memory) return;
					const uptr = buf_ptr >>> 0;
					const byteLen = width * height * 4;
					if (uptr + byteLen > exports.memory.buffer.byteLength) return;

					const bytes = new Uint8Array(exports.memory.buffer, uptr, byteLen);

					if (t.gl && t.glPipe) {
						const gl = t.gl;
						gl.viewport(0, 0, canvasEl.width || width, canvasEl.height || height);
						gl.useProgram(t.glPipe.prog);
						if (t.glPipe.uTime) gl.uniform1f(t.glPipe.uTime, performance.now() * 0.001);
						if (t.glPipe.uRes) gl.uniform2f(t.glPipe.uRes, width, height);
						gl.bindTexture(gl.TEXTURE_2D, t.glPipe.tex);
						gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, bytes);
						gl.drawArrays(gl.TRIANGLES, 0, 6);
						return;
					}

					if (t.canvas2d) {
						const clamped = new Uint8ClampedArray(exports.memory.buffer, uptr, byteLen);
						const imgData = new ImageData(clamped, width, height);
						t.canvas2d.putImageData(imgData, 0, 0);
					}
				},
				lam_fill() {},
				lam_tile_draw() {},
				lam_flush() {}
			};
		},
		destroy() {
			// Targets are keyed weakly by element; nothing to release.
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
