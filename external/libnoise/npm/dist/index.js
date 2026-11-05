import { loadWasmModule, loadNativeAddon, getSubstrate } from "@tty-pt/stele";

const nativeBinding = loadNativeAddon(import.meta.url, "noise");
let wasmInstance = null;

export function getNoiseWasm() {
  if (wasmInstance) return wasmInstance;
  wasmInstance = loadWasmModule(new URL("./noise.wasm", import.meta.url));
  return wasmInstance;
}

export function noisePoint1D(x, seed, shift) {
  if (nativeBinding && nativeBinding.point1D) {
    return nativeBinding.point1D(x, seed, shift);
  }
  const wasm = getNoiseWasm();
  return wasm.exports.noise_wasm_point_1d(x, seed, shift);
}

export function noisePoint2D(x, y, seed, shift) {
  if (nativeBinding && nativeBinding.point2D) {
    return nativeBinding.point2D(x, y, seed, shift);
  }
  const wasm = getNoiseWasm();
  return wasm.exports.noise_wasm_point_2d(x, y, seed, shift);
}

export function noisePoint3D(x, y, z, seed, shift) {
  if (nativeBinding && nativeBinding.point3D) {
    return nativeBinding.point3D(x, y, z, seed, shift);
  }
  const wasm = getNoiseWasm();
  return wasm.exports.noise_wasm_point_3d(x, y, z, seed, shift);
}

export function noisePoint4D(x, y, z, w, seed, shift) {
  if (nativeBinding && nativeBinding.point4D) {
    return nativeBinding.point4D(x, y, z, w, seed, shift);
  }
  const wasm = getNoiseWasm();
  return wasm.exports.noise_wasm_point_4d(x, y, z, w, seed, shift);
}

export function noisePointN(coords, seed, shift) {
  if (nativeBinding && nativeBinding.pointN) {
    return nativeBinding.pointN(coords, seed, shift);
  }
  const wasm = getNoiseWasm();
  const substrate = getSubstrate();
  const ptr = wasm.exports.malloc(coords.length * 2);
  const view = new Int16Array(substrate.memory.buffer, ptr, coords.length);
  for (let i = 0; i < coords.length; i++) view[i] = coords[i];
  const res = wasm.exports.noise_wasm_point_n(ptr, seed, shift, coords.length);
  wasm.exports.free(ptr);
  return res;
}

export function noiseOctaves(dim, origin, octaves, seed, cy) {
  if (nativeBinding && nativeBinding.octaves) {
    return nativeBinding.octaves(dim, origin, octaves, seed, cy);
  }
  const wasm = getNoiseWasm();
  const substrate = getSubstrate();
  const total = 1 << (dim * cy);
  const outPtr = wasm.exports.malloc(total * 4);
  const origPtr = wasm.exports.malloc(dim * 2);
  const origView = new Int16Array(substrate.memory.buffer, origPtr, dim);
  for (let i = 0; i < dim; i++) origView[i] = origin[i] || 0;

  const octCount = Math.min(octaves.length, 16);
  const octPtr = wasm.exports.malloc(octCount * 8);
  const octView = new Uint32Array(substrate.memory.buffer, octPtr, octCount * 2);
  for (let i = 0; i < octCount; i++) {
    octView[i * 2] = octaves[i].x;
    octView[i * 2 + 1] = octaves[i].w;
  }

  wasm.exports.noise_wasm_octaves(outPtr, origPtr, octCount, octPtr, seed, cy, dim);
  const res = new Uint32Array(substrate.memory.buffer, outPtr, total).slice();

  wasm.exports.free(outPtr);
  wasm.exports.free(origPtr);
  wasm.exports.free(octPtr);

  return res;
}
