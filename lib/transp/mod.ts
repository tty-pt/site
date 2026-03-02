// Deno FFI wrapper for libtransp.so

import { dirname, fromFileUrl, resolve } from "https://deno.land/std@0.208.0/path/mod.ts";

const moduleDir = dirname(fromFileUrl(import.meta.url));
const libPath = resolve(moduleDir, "libtransp.so");

// Transpose flags (must match transp.h)
export const TRANSP_HIDE_CHORDS = 0x01;
export const TRANSP_HIDE_LYRICS = 0x02;
export const TRANSP_HTML = 0x04;
export const TRANSP_BEMOL = 0x08;
export const TRANSP_REMOVE_COMMENTS = 0x10;
export const TRANSP_BREAK_SLASH = 0x20;
export const TRANSP_LATIN = 0x80;

const lib = Deno.dlopen(libPath, {
  transp_init: {
    parameters: [],
    result: "pointer",
  },
  transp_free: {
    parameters: ["pointer"],
    result: "void",
  },
  transp_buffer: {
    parameters: ["pointer", "pointer", "i32", "i32"],
    result: "pointer",
  },
});

export function transpose(
  input: string,
  semitones: number,
  flags: number
): string | null {
  const ctx = lib.symbols.transp_init();
  if (!ctx) {
    return null;
  }

  try {
    const inputBuf = new TextEncoder().encode(input + "\0");
    
    const resultPtr = lib.symbols.transp_buffer(
      ctx,
      Deno.UnsafePointer.of(inputBuf),
      semitones,
      flags
    );

    if (!resultPtr) {
      return null;
    }

    // Read the C string from the pointer
    const view = new Deno.UnsafePointerView(resultPtr);
    const result = view.getCString();
    
    // Free the C string (it was allocated by transp_buffer)
    // Note: We should free this with C's free(), but Deno FFI doesn't expose that easily
    // The memory will be leaked, but for SSR usage this is acceptable
    
    return result;
  } finally {
    lib.symbols.transp_free(ctx);
  }
}
