// HIGH_LEVEL: #surface — viewer key grammar, zero inference.
// HIGH_LEVEL: #commands — /quest plan opens the viewer.
// SPEC: B1.3 (draft stays inspectable), B1.8 (amendments stay inspectable).
//
// Local key normalizer: legacy ANSI + Kitty CSI-u + single printables,
// transcribed from pi-tui's keys.js grammar (LEGACY_SEQUENCE_KEY_IDS,
// parseKittySequence, KITTY_FUNCTIONAL_KEY_EQUIVALENTS). Lets viewers scroll
// with zero imports; kit.matchesKey stays first consult when present.

export const ESC = "\x1b";

export type KeyMatcher = (data: string, key: string) => boolean;

export type ViewerAction = "close" | "up" | "down" | "pgup" | "pgdn" | "halfup" | "halfdown" | "top" | "bottom" | null;

const LEGACY_PAIRS: Array<[string[], string]> = [
  [["\x1b[A", "\x1bOA"], "up"],
  [["\x1b[B", "\x1bOB"], "down"],
  [["\x1b[H", "\x1bOH", "\x1b[1~", "\x1b[7~"], "home"],
  [["\x1b[F", "\x1bOF", "\x1b[4~", "\x1b[8~"], "end"],
  [["\x1b[5~", "\x1b[[5~"], "pageUp"],
  [["\x1b[6~", "\x1b[[6~"], "pageDown"],
];

const LEGACY_TOKENS: Record<string, string> = Object.fromEntries(
  LEGACY_PAIRS.flatMap(([seqs, token]) => seqs.map((seq): [string, string] => [seq, token])),
);

const KITTY_ARROWS: Record<string, string> = { A: "up", B: "down", C: "right", D: "left" };

const KITTY_FUNCTIONAL: Record<number, string> = {
  57417: "left", 57418: "right", 57419: "up", 57420: "down",
  57421: "pageUp", 57422: "pageDown", 57423: "home", 57424: "end",
};

const CTRL_MOD = 4;

export function normalizeKey(data: string): string | null {
  if (data === "") return null;
  if (data === ESC) return "escape";
  if (data === "\r" || data === "\n") return "return";
  if (data === "\x15") return "ctrlU";
  if (data === "\x04") return "ctrlD";
  if (data === "\x11") return "ctrlQ";
  if (Array.from(data).length === 1) return data;
  if (data.includes("\x1b[200~")) return null;
  if (data.includes(":3u")) return null;
  const legacy = LEGACY_TOKENS[data];
  if (legacy !== undefined) return legacy;
  const arrowMod = data.match(/^\x1b\[1;\d+(?::\d+)?([ABCD])$/);
  if (arrowMod) return KITTY_ARROWS[arrowMod[1] as string] ?? null;
  const csiU = data.match(/^\x1b\[(\d+)(?::(\d*))?(?::(\d+))?(?:;(\d+))?(?::(\d+))?u$/);
  if (csiU) {
    if (csiU[5] === "3") return null;
    const codepoint = parseInt(csiU[1] as string, 10);
    const functional = KITTY_FUNCTIONAL[codepoint];
    if (functional !== undefined) return functional;
    const modifier = csiU[4] !== undefined && csiU[4] !== "" ? parseInt(csiU[4], 10) - 1 : 0;
    if ((modifier & CTRL_MOD) !== 0) {
      if (codepoint === 85 || codepoint === 117) return "ctrlU";
      if (codepoint === 68 || codepoint === 100) return "ctrlD";
      if (codepoint === 81 || codepoint === 113) return "ctrlQ";
      return null;
    }
    if (codepoint === 13) return "return";
    if (codepoint === 27) return "escape";
    if (codepoint >= 32 && codepoint <= 126) return String.fromCodePoint(codepoint);
    return null;
  }
  const tilde = data.match(/^\x1b\[(\d+)(?:;\d+)?~$/);
  if (tilde) {
    const keyNum = parseInt(tilde[1] as string, 10);
    if (keyNum === 5) return "pageUp";
    if (keyNum === 6) return "pageDown";
    if (keyNum === 1 || keyNum === 7) return "home";
    if (keyNum === 4 || keyNum === 8) return "end";
    return null;
  }
  return null;
}

export function classifyKey(data: string, match: KeyMatcher | null): ViewerAction {
  const named = (key: string): boolean => {
    if (match === null) return false;
    try {
      return match(data, key);
    } catch {
      return false;
    }
  };
  if (named("escape")) return "close";
  const token = normalizeKey(data);
  if (token === "escape" || token === "q" || token === "Q") return "close";
  if (named("return") || token === "return") return "close";
  if (named("up") || token === "up" || token === "k" || token === "K") return "up";
  if (named("down") || token === "down" || token === "j" || token === "J") return "down";
  if (named("pageUp") || token === "pageUp") return "pgup";
  if (named("pageDown") || token === "pageDown" || token === " ") return "pgdn";
  if (token === "ctrlU") return "halfup";
  if (token === "ctrlD") return "halfdown";
  if (named("home") || token === "home" || token === "g") return "top";
  if (named("end") || token === "end" || token === "G") return "bottom";
  return null;
}
