// HIGH_LEVEL: #surface — word-wrap long draft lines to visual rows, zero inference.
// SPEC: B1.3 (draft stays inspectable), B1.8 (amendments stay inspectable).
//
// SelectList renders one row per item and truncates over-long labels, so
// long quest-draft lines otherwise get silently clipped. This module wraps
// a markdown draft into visual rows (ANSI + wide-char + tab aware) before
// either renderer sees it, so every line is fully readable.
const ANSI_RE = /\x1b\[[0-9;]*m/g;
const WIDE_RE = /[^\x00-\xff]/;

// Visible cell width, matching terminal layout: ANSI escapes are invisible,
// tabs span 3 cells, and non-ASCII (CJK / wide) chars span 2 cells.
function textWidth(text: string): number {
  let w = 0;
  for (const ch of text.replace(ANSI_RE, "")) {
    if (ch === "\t") w += 3;
    else w += WIDE_RE.test(ch) ? 2 : 1;
  }
  return w;
}

// Longest visible-safe prefix of `text` that fits `width` cells, never
// breaking an ANSI escape.
function sliceToWidth(text: string, width: number): string {
  if (textWidth(text) <= width) return text;
  let result = "";
  let seen = 0;
  let i = 0;
  while (i < text.length) {
    const ansi = text.slice(i).match(/^\x1b\[[0-9;]*m/);
    if (ansi) {
      result += ansi[0];
      i += ansi[0].length;
      continue;
    }
    const ch = text[i];
    const cw = ch === "\t" ? 3 : WIDE_RE.test(ch) ? 2 : 1;
    if (seen + cw > width) break;
    result += ch;
    seen += cw;
    i++;
  }
  return result;
}

// Break an over-wide token into rows each ≤ width, ANSI-aware.
function splitLongToken(token: string, width: number): string[] {
  const out: string[] = [];
  let rest = token;
  while (textWidth(rest) > width) {
    const head = sliceToWidth(rest, width);
    if (head === "") break;
    out.push(head);
    rest = rest.slice(head.length);
  }
  if (rest !== "") out.push(rest);
  return out;
}

// Word-wrap a single line to `width` visible cells. ANSI escapes carry over
// across breaks; tokens longer than `width` split at the last fitting cell.
export function wrapLine(text: string, width: number): string[] {
  if (width < 1) return [text];
  if (textWidth(text) <= width) return [text];
  const tokens = text.split(/(\s+)/).filter((t) => t !== "");
  const rows: string[] = [];
  let current: string[] = [];
  let currentWidth = 0;
  for (const token of tokens) {
    const isSpace = /^\s+$/.test(token);
    const tokenWidth = textWidth(token);
    if (isSpace) {
      if (currentWidth > 0 && currentWidth + tokenWidth <= width) {
        current.push(token);
        currentWidth += tokenWidth;
      }
      continue;
    }
    if (tokenWidth > width) {
      if (current.length > 0) rows.push(current.join(""));
      for (const seg of splitLongToken(token, width)) rows.push(seg);
      current = [];
      currentWidth = 0;
      continue;
    }
    if (currentWidth > 0 && currentWidth + tokenWidth > width) {
      rows.push(current.join("").trimEnd());
      current = [token];
      currentWidth = tokenWidth;
    } else {
      current.push(token);
      currentWidth += tokenWidth;
    }
  }
  if (current.length > 0) rows.push(current.join("").trimEnd());
  return rows;
}

// Split markdown into visual rows, wrapping long lines to `width` cells and
// indenting continuations by `indent` spaces so wraps hang under list
// markers. Blank lines are preserved as single-space rows.
export function wrapMarkdown(markdown: string, width: number, indent = 2): string[] {
  const rows: string[] = [];
  for (const line of markdown.split("\n")) {
    if (line.trim() === "") {
      rows.push(" ");
      continue;
    }
    const wrapped = wrapLine(line, width);
    rows.push(wrapped[0] ?? " ");
    for (const cont of wrapped.slice(1)) rows.push(" ".repeat(indent) + cont);
  }
  return rows;
}
