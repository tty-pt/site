import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { parseLogEntry } from "../formatters.ts";
import { getQuestLogPath } from "../paths.ts";
import { QuestLogEntry } from "../types.ts";
import { ExtensionContext } from "../../types.ts";

export function readQuestLog(qidOrPath?: string | ExtensionContext | null, maxEntries?: number): string {
  let targetPath: string;
  if (!qidOrPath) targetPath = getQuestLogPath();
  else if (typeof qidOrPath !== "string") targetPath = getQuestLogPath(qidOrPath);
  else if (qidOrPath.endsWith(".log") || qidOrPath.includes("/")) targetPath = qidOrPath;
  else targetPath = getQuestLogPath(qidOrPath);
  if (!existsSync(targetPath)) return "";
  try {
    const content = readFileSync(targetPath, "utf8");
    if (typeof maxEntries === "number" && maxEntries > 0) {
      const lines = content.split(/\r?\n/).filter((l) => l.trim().length > 0);
      return lines.slice(-maxEntries).join("\n");
    }
    return content;
  } catch { return ""; }
}

export function parseQuestLogEntries(rawOrPath: string): QuestLogEntry[] {
  let raw = rawOrPath;
  if (!raw.includes("[") && !raw.includes("]") && (raw.endsWith(".log") || !raw.includes(" | "))) raw = readQuestLog(rawOrPath);
  const lines = raw.split(/\r?\n/).filter((l) => l.trim().length > 0);
  const entries: QuestLogEntry[] = [];
  for (const line of lines) { const entry = parseLogEntry(line); if (entry) entries.push(entry); }
  return entries;
}

export function clearQuestLog(qidOrCtx?: string | ExtensionContext | null): void {
  let p = "";
  if (typeof qidOrCtx === "string" && (qidOrCtx.endsWith(".log") || qidOrCtx.includes("/"))) p = qidOrCtx;
  else p = getQuestLogPath(qidOrCtx);
  try { if (existsSync(p)) writeFileSync(p, "", "utf8"); } catch {}
}
