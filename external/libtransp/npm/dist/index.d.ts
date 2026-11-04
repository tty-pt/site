export declare const TRANSP_HIDE_CHORDS: number;
export declare const TRANSP_HIDE_LYRICS: number;
export declare const TRANSP_HTML: number;
export declare const TRANSP_BEMOL: number;
export declare const TRANSP_REMOVE_COMMENTS: number;
export declare const TRANSP_BREAK_SLASH: number;
export declare const TRANSP_LATIN: number;

export declare function transposeText(
  text: string,
  semitones: number,
  flags?: number
): string;

export declare function getDetectedKey(): number;
export declare function resetKey(): void;
export declare function getShiftTable(latin?: number): string;

export declare function getTranspWasm(): WebAssembly.Instance;
