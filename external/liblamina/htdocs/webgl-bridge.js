/*
 * webgl-bridge.js — Browser WebAssembly bridge for WebGL 1/2 graphics.
 * Provides the "webgl" WASM import module.
 */

function readStr(memory, ptr, len) {
	if (!memory || !ptr || len <= 0) return '';
	return new TextDecoder().decode(new Uint8Array(memory.buffer, ptr, len));
}

export function createWebGlBridge(root, getWasm) {
	let gl = null;
	const shaders = [null];
	const programs = [null];
	const buffers = [null];
	const uniformLocs = [null];

	function getGl() {
		if (gl) return gl;
		const canvas = root ? (root.querySelector('[data-bud-webgl]') || root.querySelector('canvas')) : null;
		if (canvas) {
			gl = canvas.getContext('webgl2') || canvas.getContext('webgl');
		}
		return gl;
	}

	return {
		mount() {
			getGl();
		},
		destroy() {
			gl = null;
		},
		makeImports() {
			return {
				glViewport(x, y, w, h) {
					const c = getGl();
					if (c) c.viewport(x, y, w, h);
				},
				glClearColor(r, g, b, a) {
					const c = getGl();
					if (c) c.clearColor(r, g, b, a);
				},
				glClear(mask) {
					const c = getGl();
					if (c) c.clear(mask);
				},
				glCreateShader(type) {
					const c = getGl();
					if (!c) return 0;
					const s = c.createShader(type);
					shaders.push(s);
					return shaders.length - 1;
				},
				glShaderSource(shaderId, srcPtr, srcLen) {
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return;
					const src = readStr(exports.memory, srcPtr, srcLen);
					const s = shaders[shaderId];
					if (s) c.shaderSource(s, src);
				},
				glCompileShader(shaderId) {
					const c = getGl();
					const s = shaders[shaderId];
					if (c && s) c.compileShader(s);
				},
				glGetShaderParameter(shaderId, pname) {
					const c = getGl();
					const s = shaders[shaderId];
					if (!c || !s) return 0;
					const res = c.getShaderParameter(s, pname);
					return res ? 1 : 0;
				},
				glCreateProgram() {
					const c = getGl();
					if (!c) return 0;
					const p = c.createProgram();
					programs.push(p);
					return programs.length - 1;
				},
				glAttachShader(programId, shaderId) {
					const c = getGl();
					const p = programs[programId];
					const s = shaders[shaderId];
					if (c && p && s) c.attachShader(p, s);
				},
				glLinkProgram(programId) {
					const c = getGl();
					const p = programs[programId];
					if (c && p) c.linkProgram(p);
				},
				glGetProgramParameter(programId, pname) {
					const c = getGl();
					const p = programs[programId];
					if (!c || !p) return 0;
					const res = c.getProgramParameter(p, pname);
					return res ? 1 : 0;
				},
				glUseProgram(programId) {
					const c = getGl();
					const p = programs[programId];
					if (c) c.useProgram(p || null);
				},
				glCreateBuffer() {
					const c = getGl();
					if (!c) return 0;
					const b = c.createBuffer();
					buffers.push(b);
					return buffers.length - 1;
				},
				glBindBuffer(target, bufferId) {
					const c = getGl();
					const b = buffers[bufferId];
					if (c) c.bindBuffer(target, b || null);
				},
				glBufferData(target, dataPtr, size, usage) {
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return;
					const view = new Uint8Array(exports.memory.buffer, dataPtr, size);
					c.bufferData(target, view, usage);
				},
				glEnableVertexAttribArray(index) {
					const c = getGl();
					if (c) c.enableVertexAttribArray(index);
				},
				glDisableVertexAttribArray(index) {
					const c = getGl();
					if (c) c.disableVertexAttribArray(index);
				},
				glVertexAttribPointer(index, size, type, normalized, stride, offset) {
					const c = getGl();
					if (c) c.vertexAttribPointer(index, size, type, !!normalized, stride, offset);
				},
				glDrawArrays(mode, first, count) {
					const c = getGl();
					if (c) c.drawArrays(mode, first, count);
				},
				glGetUniformLocation(programId, namePtr, nameLen) {
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return -1;
					const name = readStr(exports.memory, namePtr, nameLen);
					const p = programs[programId];
					if (!p) return -1;
					const loc = c.getUniformLocation(p, name);
					if (!loc) return -1;
					uniformLocs.push(loc);
					return uniformLocs.length - 1;
				},
				glUniform1f(locId, v0) {
					const c = getGl();
					const loc = uniformLocs[locId];
					if (c && loc) c.uniform1f(loc, v0);
				},
				glUniform2f(locId, v0, v1) {
					const c = getGl();
					const loc = uniformLocs[locId];
					if (c && loc) c.uniform2f(loc, v0, v1);
				},
				glUniform3f(locId, v0, v1, v2) {
					const c = getGl();
					const loc = uniformLocs[locId];
					if (c && loc) c.uniform3f(loc, v0, v1, v2);
				},
				glUniform4f(locId, v0, v1, v2, v3) {
					const c = getGl();
					const loc = uniformLocs[locId];
					if (c && loc) c.uniform4f(loc, v0, v1, v2, v3);
				},
				glUniformMatrix4fv(locId, count, trans, valPtr) {
					const c = getGl();
					const exports = getWasm();
					const loc = uniformLocs[locId];
					if (!c || !loc || !exports || !exports.memory) return;
					const floats = new Float32Array(exports.memory.buffer, valPtr, count * 16);
					c.uniformMatrix4fv(loc, !!trans, floats);
				},
				glDeleteShader(shaderId) {
					const c = getGl();
					const s = shaders[shaderId];
					if (c && s) c.deleteShader(s);
					shaders[shaderId] = null;
				},
				glDeleteProgram(programId) {
					const c = getGl();
					const p = programs[programId];
					if (c && p) c.deleteProgram(p);
					programs[programId] = null;
				},
				glDeleteBuffer(bufferId) {
					const c = getGl();
					const b = buffers[bufferId];
					if (c && b) c.deleteBuffer(b);
					buffers[bufferId] = null;
				}
			};
		}
	};
}

if (typeof window !== 'undefined' && window.registerWasmCapability) {
	window.registerWasmCapability('webgl', createWebGlBridge);
}
