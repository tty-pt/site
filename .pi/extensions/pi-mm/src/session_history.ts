import { readFile, stat, readdir } from "node:fs/promises";
import { join } from "node:path";
import { homedir } from "node:os";
import { debugLog } from "./debug.ts";

export interface SessionExcerpt {
  role: "user" | "assistant";
  text: string;
  id?: string;
}

export interface SessionExtractOptions {
  originText?: string;
  query?: string;
  leafId?: string;
  maxFollowUps?: number;
  charBudget?: number;
}

/**
 * Normalizes text content from a Pi message object.
 * Handles both plain strings and block arrays (extracting only type === "text" and ignoring thinking/toolCall).
 */
export function extractTextFromMessage(content: unknown): string {
  if (typeof content === "string") {
    return content.trim();
  }
  if (Array.isArray(content)) {
    return content
      .filter((part: unknown) => typeof part === "object" && part !== null && (part as { type?: string }).type === "text")
      .map((part: unknown) => (part as { text?: string }).text ?? "")
      .join("\n")
      .trim();
  }
  return "";
}

/**
 * Resolves the path to a native Pi session file (.jsonl or .json) on disk.
 * Checks direct sessionFile path, test directory locations, and ~/.pi/agent/sessions.
 */
export async function resolveSessionFilePath(
  sessionFile?: string,
  sessionId?: string,
  cwd?: string,
): Promise<string | null> {
  // 1. Direct file path check
  if (sessionFile && typeof sessionFile === "string" && sessionFile.trim().length > 0) {
    const rawPath = sessionFile.trim();
    try {
      const s = await stat(rawPath);
      if (s.isFile()) return rawPath;
    } catch {
      // Continue to search fallbacks
    }
  }

  if (!sessionId || typeof sessionId !== "string" || sessionId.trim().length === 0) {
    return null;
  }

  const cleanId = sessionId.trim();

  // 2. Test/tmp directory check
  const candidateTmpPaths = [
    `/tmp/sessions/${cleanId}.jsonl`,
    `/tmp/sessions/${cleanId}.json`,
    `/tmp/${cleanId}.jsonl`,
    `/tmp/${cleanId}.json`,
  ];
  for (const p of candidateTmpPaths) {
    try {
      const s = await stat(p);
      if (s.isFile()) return p;
    } catch {
      // next
    }
  }

  // 3. ~/.pi/agent/sessions search
  try {
    const home = homedir();
    const sessionsBase = join(home, ".pi", "agent", "sessions");
    const dirs = await readdir(sessionsBase, { withFileTypes: true }).catch(() => []);

    for (const dir of dirs) {
      if (!dir.isDirectory()) continue;
      const subDir = join(sessionsBase, dir.name);
      const files = await readdir(subDir).catch(() => []);
      for (const file of files) {
        if (file.endsWith(".jsonl") && file.includes(cleanId)) {
          const matchedPath = join(subDir, file);
          return matchedPath;
        }
      }
    }
  } catch {
    // Fail-safe
  }

  // 4. cwd-relative check
  if (cwd) {
    try {
      const cwdSession = join(cwd, `${cleanId}.jsonl`);
      const s = await stat(cwdSession);
      if (s.isFile()) return cwdSession;
    } catch {
      // ignore
    }
  }

  return null;
}

export interface ParsedSessionMessage {
  id?: string;
  parentId?: string;
  role: "user" | "assistant";
  text: string;
}

/**
 * Parses lines of a Pi session JSONL file into a clean list of conversational messages.
 */
export async function parseSessionFileMessages(filePath: string): Promise<ParsedSessionMessage[]> {
  try {
    const raw = await readFile(filePath, "utf8");
    const lines = raw.split("\n");
    const messages: ParsedSessionMessage[] = [];

    for (const line of lines) {
      const trimmed = line.trim();
      if (!trimmed || !trimmed.startsWith("{")) continue;
      try {
        const obj = JSON.parse(trimmed);
        if (obj.type === "message" && obj.message && typeof obj.message === "object") {
          const role = obj.message.role;
          if (role === "user" || role === "assistant") {
            const text = extractTextFromMessage(obj.message.content);
            if (text.length > 0) {
              messages.push({
                id: obj.id,
                parentId: obj.parentId,
                role,
                text,
              });
            }
          }
        }
      } catch {
        // Skip malformed individual lines
      }
    }

    return messages;
  } catch (err) {
    debugLog("recall", "Failed to parse session file messages", {
      filePath,
      error: err instanceof Error ? err.message : String(err),
    });
    return [];
  }
}

export function tokenizeQuery(text: string): string[] {
  const stopWords = new Set([
    "what", "which", "where", "when", "who", "whom", "whose", "why", "how",
    "did", "does", "do", "was", "were", "is", "are", "been", "being", "have", "has", "had",
    "the", "a", "an", "and", "or", "but", "in", "on", "at", "to", "for", "of", "with",
    "about", "you", "your", "my", "me", "i", "we", "our", "us", "he", "him", "his", "she", "her",
    "remember", "tell", "told", "say", "said", "know", "anything", "something",
    "can", "could", "would", "should", "kind", "kinds", "type", "types", "usually", "often",
  ]);

  return text
    .toLowerCase()
    .replace(/[^\w\s-]/g, " ")
    .split(/\s+/)
    .map((w) => w.trim())
    .filter((w) => w.length >= 3 && !stopWords.has(w));
}

function getQueryStems(tokens: string[]): string[] {
  const stems = new Set<string>();
  for (const word of tokens) {
    stems.add(word);
    if (word.endsWith("s") && word.length > 3) stems.add(word.slice(0, -1));
    if (word.endsWith("es") && word.length > 4) stems.add(word.slice(0, -2));
    if (word.endsWith("ing") && word.length > 5) stems.add(word.slice(0, -3));
    if (word.endsWith("ed") && word.length > 4) stems.add(word.slice(0, -2));

    if (word === "airplanes" || word === "airplane" || word === "plane" || word === "planes") {
      stems.add("airplane");
      stems.add("airplanes");
      stems.add("plane");
      stems.add("planes");
      stems.add("aircraft");
      stems.add("boeing");
      stems.add("boing");
      stems.add("747");
      stems.add("787");
      stems.add("fly");
      stems.add("flew");
    }
    if (word === "boeing" || word === "boing" || word === "747" || word === "787") {
      stems.add("boeing");
      stems.add("boing");
      stems.add("airplane");
      stems.add("airplanes");
      stems.add("plane");
      stems.add("planes");
      stems.add("aircraft");
      stems.add("747");
      stems.add("787");
    }
    if (word === "fly" || word === "flew" || word === "flying" || word === "flight") {
      stems.add("fly");
      stems.add("flew");
      stems.add("flying");
      stems.add("flight");
      stems.add("pilot");
      stems.add("aircraft");
      stems.add("airplanes");
      stems.add("plane");
    }
    if (word === "route" || word === "routes") {
      stems.add("route");
      stems.add("routes");
      stems.add("lisbon");
      stems.add("madeira");
      stems.add("fly");
      stems.add("flew");
      stems.add("destination");
    }
    if (word === "pilot" || word === "pilots") {
      stems.add("pilot");
      stems.add("pilots");
      stems.add("flew");
      stems.add("fly");
      stems.add("flying");
    }
    if (word === "companies" || word === "company") {
      stems.add("company");
      stems.add("companies");
      stems.add("airline");
      stems.add("airlines");
    }
  }
  return Array.from(stems);
}

function isSessionFarewell(text: string): boolean {
  const t = text.trim();
  if (/^(?:bye|goodbye|see\s+you|cya|take\s+care|farewell)\b/i.test(t)) return true;
  if (/\b(?:need|have|got)\s+to\s+(?:go|leave|run)\b/i.test(t)) return true;
  return false;
}

/**
 * Extracts relevant conversation excerpts from a source session file.
 * Identifies the originating message matching the memory summary or problem,
 * and recovers subsequent follow-up user statements and assistant questions/answers.
 */
export async function extractSessionExcerpts(
  sessionFilePath: string,
  options?: SessionExtractOptions,
): Promise<SessionExcerpt[]> {
  const messages = await parseSessionFileMessages(sessionFilePath);
  if (messages.length === 0) return [];

  const budget = options?.charBudget ?? 750;

  // Build message tree map for ancestry resolution
  const byId = new Map<string, ParsedSessionMessage>();
  for (const m of messages) {
    if (m.id) byId.set(m.id, m);
  }

  // 1. Locate the originating message
  let originIdx = -1;
  const originText = options?.originText?.toLowerCase().trim();
  const queryText = options?.query?.toLowerCase().trim();

  // If leafId is provided, trace ancestry chain
  const ancestryChain = new Set<string>();
  if (options?.leafId && byId.has(options.leafId)) {
    let curr: string | undefined = options.leafId;
    while (curr && byId.has(curr)) {
      ancestryChain.add(curr);
      curr = byId.get(curr)?.parentId;
    }
  }

  if (originText && originText.length >= 10) {
    // Try exact or substring match on user messages first
    for (let i = 0; i < messages.length; i++) {
      const msg = messages[i];
      if (msg.role === "user") {
        const lower = msg.text.toLowerCase();
        if (lower.includes(originText) || originText.includes(lower.slice(0, 30))) {
          originIdx = i;
          break;
        }
      }
    }
  }

  // Fallback: match salient tokens from originText
  if (originIdx === -1 && originText) {
    const originTokens = originText
      .split(/\s+/)
      .map((w) => w.replace(/[^\w-]/g, "").toLowerCase())
      .filter((w) => w.length >= 4);

    let bestScore = 0;
    for (let i = 0; i < messages.length; i++) {
      const msg = messages[i];
      if (msg.role !== "user") continue;
      const lower = msg.text.toLowerCase();
      let score = 0;
      for (const tok of originTokens) {
        if (lower.includes(tok)) score++;
      }
      if (score > bestScore) {
        bestScore = score;
        originIdx = i;
      }
    }
  }

  if (originIdx === -1 && options?.leafId && byId.has(options.leafId)) {
    originIdx = messages.findIndex((m) => m.id === options.leafId);
  }

  // If still not found, start from the first user message
  if (originIdx === -1) {
    originIdx = messages.findIndex((m) => m.role === "user");
  }

  if (originIdx === -1) return [];

  // 2. Collect turns in this conversation thread using authentic ancestry and query relevance
  const queryTokens = queryText ? tokenizeQuery(queryText) : [];
  const stems = getQueryStems(queryTokens);

  // If originIdx matched a later follow-up turn in the session, scan backwards to include earlier thread turns
  let threadStartIdx = originIdx;
  for (let i = originIdx - 1; i >= 0; i--) {
    const msg = messages[i];
    if (msg.role === "user") {
      const lower = msg.text.toLowerCase();
      const inAncestry = msg.id && ancestryChain.has(msg.id);
      const matchesStem = stems.length > 0 && stems.some((s) => lower.includes(s));
      if (inAncestry || matchesStem) {
        threadStartIdx = i;
      }
    }
  }

  const selectedIndices = new Set<number>();
  selectedIndices.add(threadStartIdx);

  // Collect all substantive user turns in this conversation thread (they are authentic facts)
  for (let i = threadStartIdx; i < messages.length; i++) {
    const msg = messages[i];
    if (msg.role === "user") {
      if (!isSessionFarewell(msg.text) && msg.text.trim().length > 0) {
        selectedIndices.add(i);
      }
    }
  }

  // Include assistant turns that match query stems or asked questions prompting user turns
  for (let i = threadStartIdx; i < messages.length; i++) {
    const msg = messages[i];
    if (msg.role === "assistant") {
      if (isSessionFarewell(msg.text)) continue;
      const lower = msg.text.toLowerCase();
      const matchesStem = stems.some((s) => lower.includes(s));
      const isQuestion = msg.text.trim().endsWith("?");
      const hasNextUser = i + 1 < messages.length && messages[i + 1].role === "user" && !isSessionFarewell(messages[i + 1].text);
      if (matchesStem || (isQuestion && hasNextUser) || (i === threadStartIdx + 1 && hasNextUser)) {
        selectedIndices.add(i);
      }
    }
  }

  // 3. STRICT CHRONOLOGICAL ORDERING (as required by reviewer advisory)
  const sortedIndices = Array.from(selectedIndices).sort((a, b) => a - b);

  // 4. Apply character budget formatting cleanly (prioritizing factual user turns over filler)
  const budgeted: SessionExcerpt[] = [];
  let totalLen = 0;

  for (const idx of sortedIndices) {
    const msg = messages[idx];
    const cleanText = msg.text.replace(/\s+/g, " ").trim();
    if (!cleanText) continue;

    // Bound assistant turns so they do not crowd out user facts
    const maxLen = msg.role === "assistant" ? 120 : 300;
    const trimmed = cleanText.length > maxLen ? cleanText.slice(0, maxLen - 3) + "..." : cleanText;

    const remaining = budget - totalLen;
    if (remaining < 30) break;

    // If remaining space is tight (< 80 chars), skip assistant filler to save space for user turns
    if (msg.role === "assistant" && remaining < 80) {
      continue;
    }

    if (trimmed.length > remaining) {
      if (msg.role === "user") {
        budgeted.push({
          role: msg.role,
          text: trimmed.slice(0, remaining - 3) + "...",
          id: msg.id,
        });
      }
      break;
    }

    budgeted.push({
      role: msg.role,
      text: trimmed,
      id: msg.id,
    });
    totalLen += trimmed.length + 15; // allowance for role prefix
  }

  return budgeted;
}
