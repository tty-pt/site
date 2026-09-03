export function splitBashCommandChain(cmd: string): string[] {
  const segments: string[] = [];
  let current = "";
  let inSingleQuote = false;
  let inDoubleQuote = false;
  let escaped = false;

  for (let i = 0; i < cmd.length; i++) {
    const ch = cmd[i];
    if (escaped) {
      current += ch;
      escaped = false;
      continue;
    }
    if (ch === "\\") {
      escaped = true;
      current += ch;
      continue;
    }
    if (ch === "'" && !inDoubleQuote) {
      inSingleQuote = !inSingleQuote;
      current += ch;
      continue;
    }
    if (ch === '"' && !inSingleQuote) {
      inDoubleQuote = !inDoubleQuote;
      current += ch;
      continue;
    }
    if (!inSingleQuote && !inDoubleQuote) {
      if (ch === "\n" || ch === ";") {
        if (current.trim()) segments.push(current.trim());
        current = "";
        continue;
      }
      if (ch === "&" && cmd[i + 1] === "&") {
        if (current.trim()) segments.push(current.trim());
        current = "";
        i++;
        continue;
      }
      if (ch === "|" && cmd[i + 1] === "|") {
        if (current.trim()) segments.push(current.trim());
        current = "";
        i++;
        continue;
      }
      if (ch === "&") {
        const prev = i > 0 ? cmd[i - 1] : "";
        const next = i + 1 < cmd.length ? cmd[i + 1] : "";
        if (prev === ">" || prev === "<" || next === ">") {
          current += ch;
          continue;
        }
        if (current.trim()) segments.push(current.trim());
        current = "";
        continue;
      }
      if (ch === "|") {
        const prev = i > 0 ? cmd[i - 1] : "";
        if (prev === ">") {
          current += ch;
          continue;
        }
        if (current.trim()) segments.push(current.trim());
        current = "";
        continue;
      }
    }
    current += ch;
  }
  if (current.trim()) segments.push(current.trim());
  return segments;
}

export function hasFileRedirection(segment: string): boolean {
  let stripped = "";
  let inSingleQuote = false;
  let inDoubleQuote = false;
  let escaped = false;

  for (let i = 0; i < segment.length; i++) {
    const ch = segment[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch === "\\") {
      escaped = true;
      continue;
    }
    if (ch === "'" && !inDoubleQuote) {
      inSingleQuote = !inSingleQuote;
      continue;
    }
    if (ch === '"' && !inSingleQuote) {
      inDoubleQuote = !inDoubleQuote;
      continue;
    }
    if (!inSingleQuote && !inDoubleQuote) {
      stripped += ch;
    }
  }

  const step1 = stripped.replace(/[0-9&]*>>?\s*\/dev\/null/g, " ");
  const step2 = step1.replace(/[0-9&]*>>?&[0-9\-]+/g, " ");
  const step3 = step2.replace(/[0-9&]*<&[0-9\-]+/g, " ");

  return />/.test(step3);
}

export function cleanCommandPreamble(cmdSegment: string): string {
  let cleaned = cmdSegment.trim();
  while (/^[A-Za-z_][A-Za-z0-9_]*=(?:'[^']*'|"[^"]*"|\S+)\s+/.test(cleaned)) {
    cleaned = cleaned.replace(
      /^[A-Za-z_][A-Za-z0-9_]*=(?:'[^']*'|"[^"]*"|\S+)\s+/,
      "",
    );
  }
  cleaned = cleaned.trim();

  while (
    /^(?:sudo|time|nice|nohup|env|command|builtin|exec)\s+/.test(cleaned)
  ) {
    cleaned = cleaned.replace(
      /^(?:sudo|time|nice|nohup|env|command|builtin|exec)\s+/,
      "",
    ).trim();
  }
  return cleaned;
}

export function splitBashTokens(cleaned: string): string[] {
  const tokens: string[] = [];
  let token = "";
  let inSingle = false;
  let inDouble = false;
  let escaped = false;
  for (let i = 0; i < cleaned.length; i++) {
    const ch = cleaned[i];
    if (escaped) {
      token += ch;
      escaped = false;
      continue;
    }
    if (ch === "\\") {
      escaped = true;
      token += ch;
      continue;
    }
    if (ch === "'" && !inDouble) {
      inSingle = !inSingle;
      token += ch;
      continue;
    }
    if (ch === '"' && !inSingle) {
      inDouble = !inDouble;
      token += ch;
      continue;
    }
    if (!inSingle && !inDouble && /\s/.test(ch)) {
      if (token) {
        tokens.push(token);
        token = "";
      }
      continue;
    }
    token += ch;
  }
  if (token) tokens.push(token);
  return tokens;
}

export function isHelpOrVersionInvocation(tokens: string[]): boolean {
  return tokens.slice(1).some((t) =>
    t === "--version" || t === "-v" || t === "-V" || t === "--help" ||
    t === "-h"
  );
}
