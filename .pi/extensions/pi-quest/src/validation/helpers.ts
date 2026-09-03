import { SECTION_ALIASES } from "../constants.ts";
import { parseMarkdownSections } from "../markdown.ts";
import { isPlaceholderOrEmpty } from "../utils.ts";

export function getSecBody(sections: Map<string, any>, key: string): string {
  const aliases = [key, ...(SECTION_ALIASES[key] || [])];
  for (const a of aliases) {
    const s = sections.get(a);
    if (s && s.body) return s.body;
  }
  return "";
}

export function extractLines(body: string): string[] {
  return body
    .split(/\r?\n/)
    .map((l) =>
      l.replace(/^>\s*/, "").replace(/^[-*]\s*/, "").replace(
        /^\[[ xX]\]\s*/,
        "",
      ).trim()
    )
    .filter((l) =>
      l && l !== "-" && !l.startsWith(">") && !l.startsWith("not started ·") &&
      !isPlaceholderOrEmpty(l)
    );
}

export function canonicalizeFilePath(p: string): string {
  let s = p.toLowerCase().replace(/^\.\//, "").replace(/^\/+/, "");
  const modsIdx = s.indexOf("mods/");
  if (modsIdx !== -1) return s.slice(modsIdx + 5).replace(/^\//, "");
  for (const seg of ["docs/", "src/", "tests/", "htdocs/"]) {
    const idx = s.indexOf(seg);
    if (idx !== -1) return s.slice(idx);
  }
  const siteIdx = s.indexOf("site/");
  if (siteIdx !== -1) return s.slice(siteIdx + 5).replace(/^\//, "");
  return s.replace(/^\.\//, "").replace(/^\/+/, "");
}

export function extractFileNames(text: string): string[] {
  const matches = text.match(
    /\b[a-zA-Z0-9_\-\.\/]+\.(?:c|h|ts|js|json|mk|sh|md|txt|html|css|wasm)\b/gi,
  ) || [];
  const cleaned: string[] = [];
  for (const m of matches) {
    const norm = canonicalizeFilePath(m);
    if (!norm.includes("quest") && !cleaned.includes(norm)) cleaned.push(norm);
  }
  return cleaned;
}

export function filePathEquals(a: string, b: string): boolean {
  const ca = canonicalizeFilePath(a);
  const cb = canonicalizeFilePath(b);
  return ca === cb || ca.endsWith("/" + cb) || cb.endsWith("/" + ca);
}

export const READ_REVIEW_VERBS = [
  "read",
  "reads",
  "reading",
  "reviewed",
  "reviewing",
  "review",
  "analyzed",
  "analysed",
  "analyzing",
  "analysing",
  "analyze",
  "analyse",
  "investigated",
  "investigating",
  "investigate",
  "examined",
  "examining",
  "examine",
  "studied",
  "studying",
  "study",
  "explored",
  "exploring",
  "explore",
  "surveyed",
  "surveying",
  "survey",
  "inspected",
  "inspecting",
  "inspect",
  "verified",
  "verifying",
  "verify",
  "identified",
  "identifying",
  "identify",
  "confirmed",
  "confirming",
  "confirm",
  "documented",
  "documenting",
  "document",
  "mapped",
  "mapping",
  "map",
  "audited",
  "auditing",
  "audit",
  "looked",
  "saw",
  "observed",
  "noted",
];

export function extractIdentifiers(text: string): string[] {
  const matches = text.match(/\b[A-Z0-9_]{3,}\b/g) || [];
  const cleaned: string[] = [];
  for (const m of matches) {
    if (
      ![
        "THE",
        "AND",
        "FOR",
        "NOT",
        "YES",
        "ALL",
        "SET",
        "NEW",
        "ADD",
        "RUN",
        "GET",
        "PUT",
        "DEL",
      ].includes(m) && !cleaned.includes(m)
    ) cleaned.push(m);
  }
  return cleaned;
}

export const pastToPresentVerbs: Record<string, string[]> = {
  "added": ["add", "adding", "define", "include"],
  "created": ["create", "creating", "make"],
  "implemented": ["implement", "implementing", "write"],
  "fixed": ["fix", "fixing", "resolve"],
  "defined": ["define", "defining", "add"],
  "updated": ["update", "updating", "edit", "modify"],
  "modified": ["modify", "modifying", "update", "edit"],
  "configured": ["configure", "configuring", "set up"],
  "registered": ["register", "registering"],
  "removed": ["remove", "removing", "delete"],
  "deleted": ["delete", "deleting", "remove"],
  "built": ["build", "building", "compile"],
  "compiled": ["compile", "compiling", "build"],
  "verified": ["verify", "verifying", "check"],
  "tested": ["test", "testing"],
};

// Write/change verbs that DO modify files. Base = pastToPresentVerbs keys + the
// refactor family and other explicit change verbs.
export const WRITE_MODIFY_VERBS = [
  ...Object.keys(pastToPresentVerbs),
  "refactored",
  "rewrote",
  "rewritten",
  "changed",
  "wrote",
  "written",
  "moved",
  "renamed",
  "corrected",
  "extracted",
  "integrated",
];

// Classify a Completed/Reassessment statement: does it describe a file
// modification? Read/review/research-led statements (e.g. "Read all major UX
// files: X.c, Y.c") record what the agent examined, NOT what it changed, so
// they must not require entries under Files Modified. Only statements that
// carry an explicit write/change verb and are not research-led count.
export function isModificationStatement(stmt: string): boolean {
  const s = stmt.trim().toLowerCase().replace(/^[>\-*\s\[\]xX]*/, "");
  const firstToken = (s.split(/\s+/)[0] ?? "").replace(/[^a-z]/g, "");
  if (READ_REVIEW_VERBS.includes(firstToken)) return false;
  for (const v of WRITE_MODIFY_VERBS) {
    if (
      s.includes(v + " ") || s.includes(v + ":") || s.endsWith(v) ||
      s.endsWith(v + ".")
    ) {
      return true;
    }
  }
  return false;
}
