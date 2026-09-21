// HIGH_LEVEL: #validation — extractQuestTranscript builds the bounded in-brief
// for the analysis-quest validator (D4): user/assistant message text from the
// current session plus child-research sibling sessions, tail-bounded and
// truncation-marked so the validator never faces the full transcript.
import { homedir } from "node:os";
import { readdir, readFile, stat } from "node:fs/promises";
import { join } from "node:path";
import type { PiCtx, TranscriptEntry } from "../hooks/events";
import { decodeSnapshot, SNAPSHOT_TYPE } from "./snapshots";

export const TRANSCRIPT_EXTRACT_LIMIT = 8000;
export const TRANSCRIPT_OMITTED_MARK = "\n[… earlier transcript omitted]\n";

const SCAN_FILES = 60;
const SCAN_TAIL_LINES = 400;

export interface TranscriptExtractOptions {
  maxChars?: number;
  sessionsDir?: string;
}

// Message-like data shape: role + content, possibly nested under `message`.
// Shape-tolerant: unknown layouts degrade to no text rather than throwing.
export function messageText(data: unknown): string | null {
  if (typeof data === "string") return data.trim() === "" ? null : data.trim();
  if (typeof data !== "object" || data === null) return null;
  const record = data as Record<string, unknown>;
  const role = record["role"];
  if (typeof role === "string") {
    // Tool and system traffic is research spam, not substance: only the
    // user/assistant thread describes the work actually done.
    if (role !== "user" && role !== "assistant") return null;
    const text = contentText(record["content"]);
    return text === "" ? null : `[${role}] ${text}`;
  }
  if (typeof record["text"] === "string") {
    return record["text"].trim() === "" ? null : record["text"].trim();
  }
  const inner = record["message"];
  if (typeof inner === "object" && inner !== null) return messageText(inner);
  return null;
}

function contentText(content: unknown): string {
  if (typeof content === "string") return content.trim();
  if (Array.isArray(content)) {
    const parts: string[] = [];
    for (const block of content) {
      if (typeof block === "string") {
        if (block.trim() !== "") parts.push(block.trim());
        continue;
      }
      if (typeof block !== "object" || block === null) continue;
      const rec = block as Record<string, unknown>;
      const type = rec["type"];
      if (type === "tool_use" || type === "tool_result") continue;
      const text = rec["text"];
      if (typeof text === "string" && text.trim() !== "") parts.push(text.trim());
    }
    return parts.join("\n");
  }
  if (typeof content === "object" && content !== null) {
    const text = (content as Record<string, unknown>)["text"];
    if (typeof text === "string") return text.trim();
    return JSON.stringify(content);
  }
  return "";
}

async function sessionFiles(sessionsDir: string): Promise<Array<{ path: string; mtime: number }>> {
  const files: Array<{ path: string; mtime: number }> = [];
  const projects = await readdir(sessionsDir);
  for (const project of projects) {
    let entries: string[];
    try {
      entries = await readdir(join(sessionsDir, project));
    } catch {
      continue;
    }
    for (const name of entries) {
      if (!name.endsWith(".jsonl")) continue;
      const path = join(sessionsDir, project, name);
      try {
        const info = await stat(path);
        files.push({ path, mtime: info.mtimeMs });
      } catch {
        // Unreadable files are skipped.
      }
    }
  }
  files.sort((a, b) => b.mtime - a.mtime);
  return files.slice(0, SCAN_FILES);
}

async function tailLines(path: string): Promise<string[]> {
  const text = await readFile(path, "utf8");
  return text.split("\n").filter((line) => line.trim() !== "").slice(-SCAN_TAIL_LINES);
}

function parseLine(line: string): Array<Record<string, unknown>> | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(line);
  } catch {
    return null;
  }
  if (typeof parsed !== "object" || parsed === null) return null;
  const rec = parsed as Record<string, unknown>;
  if (typeof rec["customType"] === "string" && rec["customType"] !== "") return null;
  return [rec, ...(typeof rec["data"] === "object" && rec["data"] !== null ? [rec["data"] as Record<string, unknown>] : [])];
}

// A sibling session relates to a quest when it carries a snapshot for the
// quest itself or one of its children: child-research sessions then contribute
// their user/assistant thread to the brief.
async function relatedSessionPaths(sessionsDir: string, qid: string): Promise<Array<{ path: string; mtime: number }>> {
  let files: Array<{ path: string; mtime: number }>;
  try {
    files = await sessionFiles(sessionsDir);
  } catch {
    return [];
  }
  const related: Array<{ path: string; mtime: number }> = [];
  for (const file of files) {
    let lines: string[];
    try {
      lines = await tailLines(file.path);
    } catch {
      continue;
    }
    let hits = false;
    for (const line of lines) {
      let parsed: unknown;
      try {
        parsed = JSON.parse(line);
      } catch {
        continue;
      }
      if (typeof parsed !== "object" || parsed === null) continue;
      if ((parsed as Record<string, unknown>)["customType"] !== SNAPSHOT_TYPE) continue;
      const state = decodeSnapshot((parsed as Record<string, unknown>)["data"]);
      if (state === null) continue;
      if (state.qid === qid || state.parentQid === qid) {
        hits = true;
        break;
      }
    }
    if (hits) related.push(file);
  }
  return related;
}

async function sessionMessageTexts(path: string): Promise<string[]> {
  let lines: string[];
  try {
    lines = await tailLines(path);
  } catch {
    return [];
  }
  const texts: string[] = [];
  for (const line of lines) {
    const candidates = parseLine(line);
    if (candidates === null) continue;
    let found: string | null = null;
    for (const candidate of candidates) {
      found = messageText(candidate);
      if (found !== null) break;
    }
    if (found !== null) texts.push(found);
  }
  return texts;
}

export function boundTranscript(texts: readonly string[], maxChars: number): string {
  const joined = texts.map((t) => `${t}\n`).join("");
  if (joined.length <= maxChars) return joined;
  const mark = TRANSCRIPT_OMITTED_MARK;
  const keep = Math.max(0, maxChars - mark.length);
  return `${mark}${joined.slice(Math.max(0, joined.length - keep))}`;
}

export async function extractQuestTranscript(
  ctx: PiCtx,
  qid: string,
  options: TranscriptExtractOptions = {},
): Promise<string> {
  const maxChars = options.maxChars ?? TRANSCRIPT_EXTRACT_LIMIT;
  const texts: string[] = [];
  for (const entry of ctx.sessionManager.getEntries()) {
    const text = messageText(entry["data"] ?? entry);
    if (text !== null) texts.push(text);
  }
  const sessionsDir = options.sessionsDir ?? join(homedir(), ".pi", "agent", "sessions");
  const related = await relatedSessionPaths(sessionsDir, qid);
  for (const file of related) {
    texts.push(...await sessionMessageTexts(file.path));
  }
  return boundTranscript(texts, maxChars);
}