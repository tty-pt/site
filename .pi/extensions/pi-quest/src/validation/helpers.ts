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
    .map((l) => l.replace(/^>\s*/, "").replace(/^[-*]\s*/, "").replace(/^\[[ xX]\]\s*/, "").trim())
    .filter((l) => l && l !== "-" && !l.startsWith(">") && !l.startsWith("not started ·") && !isPlaceholderOrEmpty(l));
}

export function extractFileNames(text: string): string[] {
  const matches = text.match(/\b[a-zA-Z0-9_\-\.\/]+\.(?:c|h|ts|js|json|mk|sh|md|txt|html|css|wasm)\b/gi) || [];
  const cleaned: string[] = [];
  for (const m of matches) {
    const norm = m.toLowerCase().replace(/^\.\//, "");
    if (!norm.includes("quest") && !cleaned.includes(norm)) cleaned.push(norm);
  }
  return cleaned;
}

export function extractIdentifiers(text: string): string[] {
  const matches = text.match(/\b[A-Z0-9_]{3,}\b/g) || [];
  const cleaned: string[] = [];
  for (const m of matches) {
    if (!["THE", "AND", "FOR", "NOT", "YES", "ALL", "SET", "NEW", "ADD", "RUN", "GET", "PUT", "DEL"].includes(m) && !cleaned.includes(m)) cleaned.push(m);
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
