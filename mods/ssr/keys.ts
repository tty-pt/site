export const KEY_NAMES_SHARP = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
export const KEY_NAMES_FLAT = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"];
export const KEY_NAMES_LATIN_SHARP = ["Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"];
export const KEY_NAMES_LATIN_FLAT = ["Do", "Reb", "Re", "Mib", "Mi", "Fa", "Solb", "Sol", "Lab", "La", "Sib", "Si"];

export function getKeyNames(useBemol: boolean, useLatin: boolean): string[] {
  if (useLatin) {
    return useBemol ? KEY_NAMES_LATIN_FLAT : KEY_NAMES_LATIN_SHARP;
  }
  return useBemol ? KEY_NAMES_FLAT : KEY_NAMES_SHARP;
}
