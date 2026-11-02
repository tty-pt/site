import { createRequire } from "node:module";
import path from "node:path";

let nativeBinding = null;
try {
  const req = createRequire(import.meta.url);
  nativeBinding = req("../build/Release/transp_node.node");
} catch {
  try {
    const req = createRequire(import.meta.url);
    nativeBinding = req(path.join(process.cwd(), "build/Release/transp_node.node"));
  } catch {
    // Fallback
  }
}

export const TRANSP_HIDE_CHORDS = 0x01;
export const TRANSP_HIDE_LYRICS = 0x02;
export const TRANSP_HTML = 0x04;
export const TRANSP_BEMOL = 0x08;
export const TRANSP_REMOVE_COMMENTS = 0x10;
export const TRANSP_BREAK_SLASH = 0x20;
export const TRANSP_LATIN = 0x80;

/**
 * Transpose chord sheet or chord line by semitones (-11 to +11)
 */
export function transposeText(text, semitones, flags = 0) {
  if (!text) return "";
  if (nativeBinding && nativeBinding.transpose) {
    return nativeBinding.transpose(text, semitones, flags);
  }
  // If native addon is unavailable, return text unchanged
  return text;
}
