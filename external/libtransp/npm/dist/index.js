import { loadWasmModule, loadNativeAddon, getSubstrate } from "@tty-pt/stele";

const nativeBinding = loadNativeAddon(import.meta.url, "transp");
let wasmInstance = null;

export function getTranspWasm() {
  if (wasmInstance) return wasmInstance;
  wasmInstance = loadWasmModule(new URL("./transp.wasm", import.meta.url));
  return wasmInstance;
}

export const TRANSP_HIDE_CHORDS = 0x01;
export const TRANSP_HIDE_LYRICS = 0x02;
export const TRANSP_HTML = 0x04;
export const TRANSP_BEMOL = 0x08;
export const TRANSP_REMOVE_COMMENTS = 0x10;
export const TRANSP_BREAK_SLASH = 0x20;
export const TRANSP_LATIN = 0x80;

/**
 * Transpose chord sheet or chord line by semitones (-11 to +11) strictly via WebAssembly
 */
export function transposeText(text, semitones, flags = 0) {
  if (!text) return "";
  if (nativeBinding && nativeBinding.transpose) {
    return nativeBinding.transpose(text, semitones, flags);
  }

  const wasm = getTranspWasm();
  const substrate = getSubstrate();
  return substrate.withString(text, (inPtr) => {
    const outPtr = wasm.exports.transp_wasm_transpose(inPtr, semitones, flags);
    if (!outPtr) return text;
    const result = substrate.readString(outPtr);
    wasm.exports.free(outPtr);
    return result;
  });
}

/**
 * Returns chromatic index (0-11) of the detected musical key, or -1
 */
export function getDetectedKey() {
  const wasm = getTranspWasm();
  return wasm.exports.transp_wasm_get_key();
}

/**
 * Clears detected key for subsequent transposition operations
 */
export function resetKey() {
  const wasm = getTranspWasm();
  wasm.exports.transp_wasm_reset_key();
}

/**
 * Generates transposition shift table across all 12 musical keys
 */
export function getShiftTable(latin = 0) {
  const wasm = getTranspWasm();
  const substrate = getSubstrate();
  const outPtr = wasm.exports.transp_wasm_shift_table(latin ? 1 : 0);
  if (!outPtr) return "";
  const result = substrate.readString(outPtr);
  wasm.exports.free(outPtr);
  return result;
}
