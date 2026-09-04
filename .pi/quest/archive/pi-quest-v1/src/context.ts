import { readFileSync, statSync } from "node:fs";
import { createHash } from "node:crypto";
import { NOTES_FILE } from "./constants.ts";
import { asyncContext, getActiveContext } from "./state.ts";
import { ExtensionContext } from "./types.ts";

let cachedGuidelinesFingerprint = "";
let cachedGuidelinesValue: string | null | undefined = undefined;
let cachedNotesMtime = 0;
let cachedNotesHash = "";
let cachedNotesValue: string | null | undefined = undefined;

function hashContent(content: string | Uint8Array): string {
  return createHash("sha256").update(content).digest("hex").slice(0, 16);
}

export function withContext<T extends (...args: any[]) => any>(fn: T): T {
  return ((...args: any[]) => {
    let ctx: ExtensionContext | undefined;
    for (const arg of args) {
      if (
        arg && typeof arg === "object" &&
        ("sessionManager" in arg || "cwd" in arg || "ui" in arg)
      ) {
        ctx = arg as ExtensionContext;
        break;
      }
    }
    if (ctx) {
      return asyncContext.run(ctx, () => fn(...args));
    }
    return fn(...args);
  }) as T;
}

export function calculateCurrentTokens(ctx?: ExtensionContext): number | null {
  const c = getActiveContext(ctx);
  const usage = typeof c?.getContextUsage === "function"
    ? c.getContextUsage()
    : undefined;
  return usage?.tokens ??
    (usage?.percent && usage?.contextWindow
      ? Math.round((usage.percent * usage.contextWindow) / 100)
      : null);
}

export function usagePercent(ctx: ExtensionContext): number {
  const u = typeof ctx.getContextUsage === "function"
    ? ctx.getContextUsage()
    : undefined;
  if (u && typeof u.percent === "number" && Number.isFinite(u.percent)) {
    return u.percent;
  }
  return 0;
}

export function gitBranch(): string | null {
  try {
    const head = readFileSync(".git/HEAD", "utf8").trim();
    const m = head.match(/^ref:\s*refs\/heads\/(.+)$/);
    return m ? m[1] : head.slice(0, 40);
  } catch {
    return null;
  }
}

export function standingNotes(): string | null {
  try {
    const st = statSync(NOTES_FILE);
    const mtime = st.mtimeMs;
    if (cachedNotesValue !== undefined && cachedNotesMtime === mtime) {
      return cachedNotesValue;
    }
    const t = readFileSync(NOTES_FILE, "utf8").trim();
    const h = hashContent(t);
    if (
      cachedNotesMtime === mtime && cachedNotesHash === h &&
      cachedNotesValue !== undefined
    ) {
      return cachedNotesValue;
    }
    const val = t || null;
    cachedNotesMtime = mtime;
    cachedNotesHash = h;
    cachedNotesValue = val;
    return val;
  } catch {
    cachedNotesMtime = 0;
    cachedNotesHash = "";
    cachedNotesValue = null;
    return null;
  }
}

export function getGuidelinesFingerprint(): string {
  try {
    const candidates = ["AGENTS.md", "CLAUDE.md", "SYSTEM.md"];
    const parts: string[] = [];
    for (const file of candidates) {
      try {
        const st = statSync(file);
        parts.push(`${file}:${st.mtimeMs}:${st.size}`);
      } catch {
        parts.push(`${file}:0:0`);
      }
    }
    return parts.join("|");
  } catch {
    return "";
  }
}

export function projectGuidelines(): string | null {
  const fingerprint = getGuidelinesFingerprint();
  if (
    cachedGuidelinesValue !== undefined &&
    cachedGuidelinesFingerprint === fingerprint
  ) {
    return cachedGuidelinesValue;
  }
  const candidates = ["AGENTS.md", "CLAUDE.md", "SYSTEM.md"];
  for (const file of candidates) {
    try {
      const content = readFileSync(file, "utf8").trim();
      if (!content) continue;

      const match = content.match(
        /(##\s+(?:Guidelines|Rules|Invariants|Mandatory Guidelines)[\s\S]*?)(?=\n##\s+|$)/i,
      );
      let val: string;
      if (match && match[1].trim()) {
        val =
          `# MANDATORY PROJECT INVARIANTS & GUIDELINES (Strictly Enforced from \`${file}\`)\nCRITICAL: The following guidelines are absolute architectural invariants. You MUST strictly adhere to them across all turns without exception.\n\n${
            match[1].trim()
          }`;
      } else if (content.length <= 2500) {
        val =
          `# MANDATORY PROJECT INVARIANTS & GUIDELINES (Strictly Enforced from \`${file}\`)\nCRITICAL: The following guidelines are absolute architectural invariants. You MUST strictly adhere to them across all turns without exception.\n\n${content}`;
      } else {
        continue;
      }
      cachedGuidelinesFingerprint = fingerprint;
      cachedGuidelinesValue = val;
      return val;
    } catch {
      // file unreadable or missing
    }
  }
  cachedGuidelinesFingerprint = fingerprint;
  cachedGuidelinesValue = null;
  return null;
}
