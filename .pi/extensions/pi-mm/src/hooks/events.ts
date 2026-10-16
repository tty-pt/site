import type { EnvSource } from "../tools/index.ts";
import { axisPath, memDir, sepalConfigured } from "../tools/index.ts";
import { describeCapabilities } from "../config.ts";
import { evaluateTurnWithJudge } from "../judge/adapter.ts";
import type { JudgeDecision } from "../judge/types.ts";
import { filespecFor, buildStoreInvocation, buildListInvocation, buildGetInvocation, embedEnv } from "../qmap.ts";
import { parseBareRefs, nextRef } from "../resolve.ts";
import { encodePayload, decodePayload } from "../payload.ts";
import { withLock } from "../lock.ts";
import type { MemoryEnvelope, EpisodeRecord } from "../types.ts";
import { handleBeforeAgentStart, type BeforeAgentStartEvent } from "./auto_recall.ts";
import { validateEvidenceRef } from "../tools/lifecycle.ts";
import { executeHybridScan } from "../tools/scan.ts";
import { runAutoConsolidation } from "../tools/consolidate.ts";
import { debugLog, traceEvent } from "../debug.ts";
import { isDaemonLockHeld } from "../daemon.ts";
import { readFile, writeFile, unlink, mkdir, rename } from "node:fs/promises";
import { join } from "node:path";

export interface PiToolSpec {
  name: string; label: string; description: string; parameters: unknown;
  execute: (toolCallId: string, params: Record<string, unknown>, signal: AbortSignal | undefined, onUpdate: unknown, ctx: PiCtx) => Promise<PiAgentToolResult>;
}
export interface PiAgentToolResult { content: Array<{ type: string; text?: string }>; details?: unknown; }
export interface PiExecResult { stdout: string; stderr: string; code: number; }
export interface PiCtx {
  cwd: string;
  hasUI: boolean;
  mode: string;
  ui?: unknown;
  sessionId?: string;
  sessionManager?: {
    getSessionId?: () => string;
    getSessionFile?: () => string;
    getLeafId?: () => string;
    id?: string;
    sessionId?: string;
  };
}

export interface ToolResultEvent {
  toolCallId?: string; toolName: string; input?: Record<string, unknown>; args?: Record<string, unknown>;
  content?: Array<{ type: string; text?: string }>; isError?: boolean; details?: unknown;
}

export interface SignificantEvent {
  id: string; timestamp: string; toolName: string; summary: string; isError: boolean;
  approach?: string; outcome?: "worked" | "failed" | "abandoned"; diffs?: string[];
  environment?: Record<string, string>; toolArgs?: Record<string, unknown>;
  evidence?: number[];
}

export function extractSessionId(ctx?: PiCtx): string {
  if (!ctx) return "unknown-session";
  const sm = ctx.sessionManager ||
    ((ctx as unknown as Record<string, unknown>)?.sessionManager as
      | { getSessionId?: () => string; id?: string; sessionId?: string }
      | undefined);
  return (
    sm?.getSessionId?.() ||
    sm?.id ||
    sm?.sessionId ||
    ctx.sessionId ||
    ((ctx as unknown as Record<string, unknown>)?.sessionId as string) ||
    (typeof process !== "undefined" ? process.env?.["PI_SESSION_ID"] : undefined) ||
    (typeof Deno !== "undefined" ? Deno.env.get("PI_SESSION_ID") : undefined) ||
    "unknown-session"
  );
}

export interface Pi {
  on(event: string, handler: (...args: unknown[]) => unknown): void;
  registerTool(tool: PiToolSpec): void;
  exec(command: string, args: string[], options?: { cwd?: string; env?: Record<string, string> }): Promise<PiExecResult>;
}

export class SessionEventBuffer {
  private events: SignificantEvent[] = [];
  private inFlightBatch: SignificantEvent[] | null = null;
  private maxEvents = 50; private flushedEventIds = new Set<string>(); private journaledEventIds = new Set<string>();
  private journalDir: string | null = null; private journalQueue: Promise<void> = Promise.resolve();
  private isFlushing = false; public lastError: string | null = null; public hasOverflow = false; public overflowCount = 0;
  public _testHooks?: { onBeforeRename?: (snapshot: SignificantEvent[]) => Promise<void> | void };

  public setJournalDir(dir: string): void { this.journalDir = dir; }
  public clear(): void {
    this.events = []; this.inFlightBatch = null;
    this.flushedEventIds.clear(); this.journaledEventIds.clear();
    this.lastError = null; this.isFlushing = false; this.hasOverflow = false; this.overflowCount = 0;
  }
  public onSessionStart(): void {
    this.events = this.events.filter((e) => !this.flushedEventIds.has(e.id));
    if (!this.isFlushing) this.inFlightBatch = null;
  }
  public isJournaled(id: string): boolean { return this.journaledEventIds.has(id); }
  public record(event: SignificantEvent): void {
    if (this.flushedEventIds.has(event.id) || this.journaledEventIds.has(event.id)) return;
    const alreadyBuffered = this.events.some((e) => e.id === event.id);
    if (!alreadyBuffered) {
      if (this.events.length >= this.maxEvents) {
        this.hasOverflow = true; this.overflowCount++;
        this.lastError = `buffer-overflow: capacity exceeded (${this.maxEvents} events)`;
      }
      this.events.push(event);
    }
    if (this.journalDir) {
      void this.saveJournal(this.journalDir).catch(() => {});
    }
  }
  public async recordAsync(event: SignificantEvent): Promise<void> {
    if (this.flushedEventIds.has(event.id) || this.journaledEventIds.has(event.id)) return;
    const alreadyBuffered = this.events.some((e) => e.id === event.id);
    if (!alreadyBuffered) {
      if (this.events.length >= this.maxEvents) {
        this.hasOverflow = true; this.overflowCount++;
        this.lastError = `buffer-overflow: capacity exceeded (${this.maxEvents} events)`;
      }
      this.events.push(event);
    }
    if (!this.journalDir) {
      this.lastError = "journal-error: journalDir has not been initialized";
      throw new Error("journal-error: journalDir has not been initialized");
    }
    await this.saveJournal(this.journalDir);
  }
  public getOverflowStatus(): { hasOverflow: boolean; overflowCount: number } {
    return { hasOverflow: this.hasOverflow, overflowCount: this.overflowCount };
  }
  public getEvents(): readonly SignificantEvent[] { return this.events; }
  public hasUnflushed(): boolean { return this.events.some((e) => !this.flushedEventIds.has(e.id)); }
  public getUnflushed(): SignificantEvent[] { return this.events.filter((e) => !this.flushedEventIds.has(e.id)); }
  public beginFlush(): SignificantEvent[] | null {
    if (this.isFlushing) return null;
    const unflushed = this.getUnflushed();
    if (unflushed.length === 0) return null;
    this.isFlushing = true;
    this.inFlightBatch = [...unflushed];
    return this.inFlightBatch;
  }
  public commitFlush(ids: string[]): void {
    for (const id of ids) {
      this.flushedEventIds.add(id);
      this.journaledEventIds.delete(id);
    }
    this.inFlightBatch = null; this.isFlushing = false; this.lastError = null;
    this.events = this.events.filter((e) => !this.flushedEventIds.has(e.id));
    if (this.events.length <= this.maxEvents) { this.hasOverflow = false; this.overflowCount = 0; }
  }
  public markFlushed(_fingerprint: string, ids: string[]): void { this.commitFlush(ids); }
  public getApproaches(): Array<{ approach: string; outcome: "worked" | "failed" | "abandoned"; reason?: string }> {
    return extractBatchApproaches(this.getUnflushed());
  }
  public getDiffs(): string[] { return extractBatchDiffs(this.getUnflushed()); }
  public rollbackFlush(err?: string): void {
    this.inFlightBatch = null; this.isFlushing = false;
    if (err) this.lastError = err;
  }

  public async saveJournal(dir: string): Promise<void> {
    const journalFile = join(dir, ".pending_events.json");
    this.journalQueue = this.journalQueue.catch(() => {}).then(async () => {
      // 1. Select the snapshot inside the serialized journal operation
      const snapshot = this.getUnflushed();
      const tmpJournal = `${journalFile}.tmp.${Date.now()}.${crypto.randomUUID()}`;
      try {
        await mkdir(dir, { recursive: true });
        if (snapshot.length === 0) {
          await unlink(journalFile).catch(() => {});
        } else {
          // 2. Write that exact snapshot to the unique temporary file
          await writeFile(tmpJournal, JSON.stringify(snapshot));
          if (this._testHooks?.onBeforeRename) {
            await this._testHooks.onBeforeRename(snapshot);
          }
          // 3. Atomically rename it into place
          await rename(tmpJournal, journalFile);
        }
        // 4. Only after successful rename, mark the IDs in that snapshot as journaled
        for (const e of snapshot) {
          this.journaledEventIds.add(e.id);
        }
        if (this.lastError?.startsWith("journal-error:")) this.lastError = null;
      } catch (err: unknown) {
        await unlink(tmpJournal).catch(() => {});
        const msg = err instanceof Error ? err.message : String(err);
        this.lastError = `journal-error: ${msg}`;
        throw new Error(`journal-error: ${msg}`);
      }
    });
    await this.journalQueue;
  }

  public async restoreJournal(dir: string, existingEventIds?: Set<string>): Promise<number> {
    const journalFile = join(dir, ".pending_events.json");
    try {
      const data = await readFile(journalFile, "utf-8");
      let loaded: SignificantEvent[];
      try {
        loaded = JSON.parse(data);
        if (!Array.isArray(loaded)) throw new Error("journal content is not an array");
      } catch (parseErr: unknown) {
        const msg = parseErr instanceof Error ? parseErr.message : String(parseErr);
        this.lastError = `journal-restore-error: invalid json in ${journalFile}: ${msg}`;
        return 0;
      }
      let count = 0;
      for (const ev of loaded) {
        if (!ev || !ev.id) continue;
        if (existingEventIds?.has(ev.id)) { this.flushedEventIds.add(ev.id); continue; }
        if (!this.flushedEventIds.has(ev.id) && !this.events.some((e) => e.id === ev.id)) {
          this.events.push(ev);
          this.journaledEventIds.add(ev.id);
          count++;
        }
      }
      await this.saveJournal(dir);
      return count;
    } catch (err: unknown) {
      if ((err as { code?: string })?.code === "ENOENT") return 0; // Benign clean-start
      const msg = err instanceof Error ? err.message : String(err);
      this.lastError = `journal-restore-error: ${msg}`;
      return 0;
    }
  }
}

export const activeSessionBuffer = new SessionEventBuffer();

export function extractBatchApproaches(batch: SignificantEvent[]): Array<{ approach: string; outcome: "worked" | "failed" | "abandoned"; reason?: string }> {
  return batch.filter((e) => e.approach && e.outcome).map((e) => ({
    approach: e.approach!, outcome: e.outcome!, reason: e.summary,
  }));
}

export function extractBatchDiffs(batch: SignificantEvent[]): string[] {
  const diffs: string[] = [];
  for (const e of batch) if (e.diffs) diffs.push(...e.diffs);
  return diffs;
}

export function summarizeToolEvent(event: ToolResultEvent): SignificantEvent | null {
  const isErr = Boolean(event.isError);
  const text = event.content?.map((c) => c.text || "").join(" ").trim() || "";
  const cmd = String(event.input?.["command"] || event.args?.["command"] || "");
  const hasErrorWord = /\b(?:FAIL|failed|error|fatal|panic)\b/i.test(text.replace(/0\s+failed/gi, ""));
  const failed = isErr || hasErrorWord;

  const isVerification = /test|check|verify|smoke/i.test(event.toolName) ||
                         /test|check|make|deno|cargo|npm/i.test(cmd) ||
                         /test|PASS|check-complexity|integration|ok\s+\d+|all green/i.test(text);
  const verifiedWorked = !failed && isVerification && (/PASS|passed|all green|ok\s+-|0 failed/i.test(text));

  // Filter out routine, non-error bash invocations and low-value tool calls without verification signals
  if (event.toolName === "bash" && !failed && !verifiedWorked) {
    return null;
  }

  const isTest = isVerification;
  const isCodeMod = /edit|write|patch/i.test(event.toolName);
  if (!isTest && !failed && !isCodeMod) return null;

  const outcome: "worked" | "failed" | undefined = failed ? "failed" : verifiedWorked ? "worked" : undefined;
  const firstLine = text.split("\n")[0]?.slice(0, 160) || `Executed ${event.toolName}`;
  const id = event.toolCallId || `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
  const toolArgs = event.input ? { ...event.input } : event.args ? { ...event.args } : undefined;

  return {
    id, timestamp: new Date().toISOString(), toolName: event.toolName, summary: firstLine,
    isError: failed, approach: outcome ? `Executed ${event.toolName}: ${firstLine.slice(0, 80)}` : undefined,
    outcome, environment: { runtime: typeof Deno !== "undefined" ? "deno" : "node" }, toolArgs,
  };
}

let activeFlushPromise: Promise<number | null> | null = null;

async function executeBatchFlush(
  batch: SignificantEvent[], envSource: EnvSource, ctx: PiCtx,
): Promise<number | null> {
  const ids = batch.map((e) => e.id);
  const firstEvent = batch[0];
  const approaches = extractBatchApproaches(batch);
  const hasFail = batch.some((e) => e.isError);

  try {
    const env = await envSource(ctx);
    if (!env.cfg.qmapBin) { activeSessionBuffer.rollbackFlush("qmap-not-found"); return null; }

    const sessionManager = ctx.sessionManager ||
      ((ctx as unknown as Record<string, unknown>)?.sessionManager as
        | { getSessionId?: () => string; getSessionFile?: () => string; getLeafId?: () => string; id?: string; sessionId?: string }
        | undefined);
    const sessionId = sessionManager?.getSessionId?.() ||
      sessionManager?.id ||
      sessionManager?.sessionId ||
      ctx.sessionId ||
      ((ctx as unknown as Record<string, unknown>)?.sessionId as string) ||
      (typeof process !== "undefined" ? process.env?.["PI_SESSION_ID"] : undefined) ||
      (typeof Deno !== "undefined" ? Deno.env.get("PI_SESSION_ID") : undefined);
    const sessionFile = sessionManager?.getSessionFile?.() ||
      (typeof process !== "undefined" ? process.env?.["PI_SESSION_FILE"] : undefined) ||
      (typeof Deno !== "undefined" ? Deno.env.get("PI_SESSION_FILE") : undefined);
    const leafId = sessionManager?.getLeafId?.();

    const episode: EpisodeRecord = {
      problem: firstEvent.summary,
      approaches: approaches.length > 0 ? approaches : undefined,
      environment: firstEvent.environment,
      outcome: hasFail ? "failure" : approaches.some((a) => a.outcome === "worked") ? "success" : "observed",
      eventIds: ids,
      sessionId: sessionId ?? undefined,
      sessionFile: sessionFile ?? undefined,
      leafId: leafId ?? undefined,
      toolCallId: firstEvent.id,
    };

    // Collect any explicit evidence references from events in the batch
    const candidateEvidence: number[] = [];
    for (const e of batch) {
      if (Array.isArray(e.evidence)) {
        for (const ev of e.evidence) {
          if (Number.isInteger(ev) && ev > 0 && !candidateEvidence.includes(ev)) {
            candidateEvidence.push(ev);
          }
        }
      }
    }

    let lifecycle: import("../types.ts").LifecycleState = "observed";
    let validatedEvidence: number[] | undefined;

    if (episode.outcome === "success") {
      if (candidateEvidence.length > 0) {
        let allValid = true;
        for (const evRef of candidateEvidence) {
          const evCheck = await validateEvidenceRef(env, evRef);
          if (!evCheck.valid) {
            allValid = false;
            break;
          }
        }
        if (allValid) {
          lifecycle = "supported";
          validatedEvidence = candidateEvidence;
        } else {
          lifecycle = "tested";
        }
      } else {
        // Without validated supporting evidence, successful outcomes remain at "tested"
        lifecycle = "tested";
      }
    } else {
      lifecycle = "observed";
    }

    const envelope: MemoryEnvelope = {
      level: "L0",
      type: "episode",
      lifecycle,
      summary: firstEvent.summary,
      evidence: validatedEvidence,
      episode,
    };
    const date = env.nowProvider().toISOString().slice(0, 10);
    const payload = encodePayload(date, envelope);
    const cwd = memDir(env);
    const axes = axisPath(env);
    const embed = sepalConfigured(env.cfg);
    const fspec = filespecFor(cwd, embed);
    const eEnv = embedEnv(env.cfg);

    return await withLock(cwd, async () => {
      const list = await env.runner.run(buildListInvocation(env.cfg.qmapBin, fspec, axes, cwd, eEnv));
      const ref = nextRef(parseBareRefs(list.stdout));
      const res = await env.runner.run(buildStoreInvocation(env.cfg.qmapBin, fspec, ref, payload, axes, cwd, eEnv));
      if (res.code === 0) {
        activeSessionBuffer.commitFlush(ids);
        await activeSessionBuffer.saveJournal(cwd).catch(() => {});
        return ref;
      }
      activeSessionBuffer.rollbackFlush(`store-failed-code-${res.code}: ${res.stderr}`);
      await activeSessionBuffer.saveJournal(cwd).catch(() => {});
      return null;
    });
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    activeSessionBuffer.rollbackFlush(msg);
    return null;
  }
}

export async function flushSessionBuffer(envSource: EnvSource, ctx: PiCtx): Promise<number | null> {
  while (activeFlushPromise) { await activeFlushPromise; }
  const batch = activeSessionBuffer.beginFlush();
  if (!batch || batch.length === 0) return null;
  activeFlushPromise = executeBatchFlush(batch, envSource, ctx);
  try { return await activeFlushPromise; } finally { activeFlushPromise = null; }
}

export interface ActiveConversationTurn {
  prompt?: string;
  response?: string;
  responses?: string[];
  timestamp?: string;
  activeMemoryRef?: number;
  activeSessionId?: string;
  previousUserPrompt?: string;
  previousAssistantResponse?: string;
  topicSubject?: string;
}

export const activeConversationTurn: ActiveConversationTurn = {};

export interface ConversationalClassification {
  meaningful: boolean;
  type?: "note" | "decision" | "episode";
  level?: "L0" | "L1" | "L2";
  summary?: string;
  problem?: string;
  findings?: string[];
  evidence?: number[];
}

const TRIVIAL_PROMPT_PATTERNS = [
  /^(?:hi|hello|hey|greetings|howdy|yo)(?:\s+(?:there|everyone|all|friend|folks|team|world))?[!.?\s]*$/i,
  /^(?:good\s+(?:morning|afternoon|evening|day))[!.?\s]*$/i,
  /^(?:thanks|thank\s+you|thx|cheers|great|awesome|cool)[!.?\s]*$/i,
  /^(?:ok|okay|k|got\s+it|understood|sure|fine|yep|yes|no|nope)[!.?\s]*$/i,
  /^(?:bye|goodbye|see\s+you|cya)[!.?\s]*$/i,
  /^(?:ping|test|echo|status)[!.?\s]*$/i,
  /^(?:what\s+time\s+is\s+it|how\s+are\s+you|who\s+are\s+you)[!.?\s]*$/i,
];

const REMEMBER_PATTERNS = [
  /^(?:please\s+)?remember\s+(?:this|that|the\s+following|our)?\s*[:,\-—]?\s*(.+)/is,
  /^(?:keep\s+in\s+mind(?:\s+that)?)\s*[:,\-—]?\s*(.+)/is,
  /^(?:note\s+that|record\s+that|store\s+this(?:\s+memory)?|save\s+this(?:\s+memory)?)\s*[:,\-—]?\s*(.+)/is,
  /^(?:always\s+remember\s+that)\s*[:,\-—]?\s*(.+)/is,
];

const DECISION_PATTERNS = [
  /(?:we\s+decided\s+(?:that|to)?|decision\s*[:\-—]|architectural\s+decision\s*[:\-—])/i,
  /(?:the\s+rule\s+is|our\s+rule\s+is|convention\s+is|policy\s+is)/i,
  /(?:must\s+always|must\s+never|do\s+not\s+ever)/i,
];

const PERSONAL_EXPERIENCE_PATTERNS = [
  /\bI\s+(?:spoke|talked|met|visited|went|saw|heard|found|told|spent|remember|felt|learned|worked|lived|bought|made|stayed|walked|sat|read|wrote)\b/i,
  /\b(?:my\s+(?:grampa|grandpa|grandfather|grandmother|grandma|father|mother|dad|mom|brother|sister|son|daughter|wife|husband|partner|fiancé|fiancee|friend|colleague|cousin|family|uncle|aunt|dog|cat|pet))\b/i,
  /\b(?:I\s+(?:prefer|like|love|dislike|hate|enjoy|plan\s+to|hope\s+to|intend\s+to|want\s+to))\b/i,
  /\b(?:my\s+(?:favorite|preference|goal|plan|intention|hobby))\b/i,
  /\b(?:I\s+(?:was\s+born|grew\s+up|graduated|moved\s+to|started\s+working|got\s+married))\b/i,
];

export function isTrivialFarewell(text: string): boolean {
  const t = text.trim();
  if (!t) return false;
  if (/^(?:bye|goodbye|cya|farewell|see\s+you(?:\s+later|\s+tomorrow)?|take\s+care|have\s+a\s+good\s+(?:one|day|night))[!.?\s]*$/i.test(t)) {
    return true;
  }
  if (/\b(?:bye|goodbye|cya|see\s+you)\b/i.test(t) && /\b(?:need|have|got|must)\s+to\s+(?:go|leave|run)\b/i.test(t)) {
    return true;
  }
  if (/^\s*(?:bye|goodbye|cya)[,.\s]+i\s+(?:need|have|got|must)\s+to\s+(?:go|leave|run)[!.?\s]*$/i.test(t)) {
    return true;
  }
  if (/^\s*i\s+(?:need|have|got|must)\s+to\s+(?:go|leave|run)(?:[,.\s]+(?:bye|goodbye|cya))?[!.?\s]*$/i.test(t)) {
    return true;
  }
  return false;
}

export function isForgetDirective(text: string): boolean {
  const t = text.trim();
  if (!t) return false;
  return /\b(?:forget\s+(?:this|that|everything|our\s+previous|my|what)|do\s+not\s+remember|don't\s+remember|please\s+forget|erase\s+(?:this|that|memory))\b/i.test(t);
}

export function isTrivialGreeting(text: string): boolean {
  const t = text.trim();
  if (!t) return false;
  for (const pat of TRIVIAL_PROMPT_PATTERNS) {
    if (pat.test(t)) return true;
  }
  return false;
}

export function isMemoryInquiry(text: string): boolean {
  const t = text.trim();
  if (!t) return false;
  if (/^(?:do\s+you\s+(?:remember|recall)|can\s+you\s+(?:remember|recall)|did\s+(?:i|we)\s+(?:say|tell|mention|talk|speak|ask)\b|what\s+did\s+(?:i|we)\s+(?:say|tell|mention|talk|speak)|what\s+do\s+you\s+(?:remember|recall|know)\s+about|have\s+(?:i|we)\s+(?:ever\s+)?(?:mentioned|told\s+you|talked\s+about|spoken\s+about)|anything\s+about|remember\s+anything\s+about|remember\s+what\s+(?:i|we)|recall\s+anything\s+about)\b/i.test(t)) {
    return true;
  }
  if (/^(?:remember|recall)\b/i.test(t) && t.endsWith("?")) {
    return true;
  }
  return false;
}

export function isExplicitRememberDirective(text: string): { isDirective: boolean; fact?: string } {
  const t = text.trim();
  if (isMemoryInquiry(t)) return { isDirective: false };
  if (t.endsWith("?")) return { isDirective: false };

  for (const pat of REMEMBER_PATTERNS) {
    const m = t.match(pat);
    if (m && m[1]) {
      const fact = m[1].trim();
      if (fact.length >= 8 && !fact.endsWith("?") && !isMemoryInquiry(fact)) {
        return { isDirective: true, fact };
      }
    }
  }
  return { isDirective: false };
}

export function cleanSummarySentence(text: string): string {
  let cleaned = text
    .replace(/^(?:remember\s+(?:this|that|the\s+following|our)?|note\s+that|please\s+note\s+that|keep\s+in\s+mind(?:\s+that)?)\s*[:,\-—]?\s*/i, "")
    .replace(/^(?:understood|got\s+it|i\s+will\s+remember\s+that|noted|sure)\s*[:,\-—.]\s*/i, "")
    .trim();

  // Strip trailing period/exclamation/question mark from the very end of the summary
  cleaned = cleaned.replace(/[.!?]+$/, "").trim();

  // If text fits in 300 characters, keep the full statement intact (preserving entities and settings)
  if (cleaned.length <= 300) {
    return cleaned;
  }

  // If text exceeds 300 characters, truncate cleanly at the last sentence end within 300 chars
  const lastSentenceEnd = cleaned.slice(0, 300).search(/[.!?](?:\s+[^.!?]*$|$)/);
  if (lastSentenceEnd > 60) {
    return cleaned.slice(0, lastSentenceEnd + 1).replace(/[.!?]+$/, "").trim();
  }

  return cleaned.slice(0, 297).trim() + "...";
}

export function classifyConversationalTurn(
  prompt: string,
  response: string,
  threadContext?: string,
): ConversationalClassification {
  const p = prompt.trim();
  const r = response.trim();

  // 1. Trivial filters: very short or matching pleasantries
  if (p.length < 5 && !threadContext) return { meaningful: false };
  if (isTrivialFarewell(p)) return { meaningful: false };
  if (isForgetDirective(p)) return { meaningful: false };
  for (const pat of TRIVIAL_PROMPT_PATTERNS) {
    if (pat.test(p)) return { meaningful: false };
  }

  // 1.5. Questions inquiring about existing memory are queries, not new memories
  if (isMemoryInquiry(p)) return { meaningful: false };

  // 2. Explicit remember directives: always meaningful even if assistant reply is brief
  const rem = isExplicitRememberDirective(p);
  if (rem.isDirective && rem.fact) {
    return {
      meaningful: true,
      type: "note",
      level: "L1",
      summary: rem.fact,
      problem: p,
      findings: [rem.fact],
    };
  }

  // 2.5 Follow-ups within an ongoing conversation thread:
  // Short factual user follow-ups (e.g. "Boing 747.", "He flew from Lisbon to Madeira and back")
  const isQuestion = p.endsWith("?") || /^(?:what|who|where|when|why|how|can\s+you|could\s+you|explain|describe)\b/i.test(p);
  if (threadContext && !isQuestion && p.length >= 3) {
    const cleanSummary = cleanSummarySentence(p) || p;
    return {
      meaningful: true,
      type: "note",
      level: "L1",
      summary: cleanSummary,
      problem: p,
      findings: [cleanSummary],
    };
  }

  // 3. Explicit decisions, rules, architectural conventions
  for (const pat of DECISION_PATTERNS) {
    if (pat.test(p) || pat.test(r)) {
      const candidateSummary = p.length >= 25 && !p.endsWith("?") ? p : (r.length >= 25 ? r : `${p}: ${r}`);
      const cleanSummary = cleanSummarySentence(candidateSummary);
      if (cleanSummary.length >= 15) {
        return {
          meaningful: true,
          type: "decision",
          level: "L1",
          summary: cleanSummary,
          problem: p,
          findings: [cleanSummary],
        };
      }
    }
  }

  // 4. Personal experiences, relationships, preferences, plans, and facts
  if (!isQuestion && p.length >= 15) {
    for (const pat of PERSONAL_EXPERIENCE_PATTERNS) {
      if (pat.test(p)) {
        const cleanSummary = cleanSummarySentence(p);
        if (cleanSummary.length >= 12) {
          return {
            meaningful: true,
            type: "note",
            level: "L1",
            summary: cleanSummary,
            problem: p,
            findings: [cleanSummary],
          };
        }
      }
    }
  }

  // 5. Substantive technical problem-solving outcomes, bug diagnostics, or architectural configurations
  if (p.length >= 30 && r.length >= 30) {
    const isProblemOutcome = /(?:caused\s+by|fixed\s+by|root\s+cause|resolved\s+by|eliminated\s+.*(?:leak|hang|drop|panic|crash|error|failure)|remedy\s+was|reason\s+was|due\s+to\s+(?:a\s+)?.*(?:limited|overflow|leak|failure|bug|issue|exhaust|crash|drop)|because\s+.*(?:limited|overflow|leak|failure|bug|issue|exhaust))/i.test(r);
    const isProjectConfiguration = /(?:our\s+(?:cluster|pipeline|deployment|server|database|daemon|service)|in\s+our\s+(?:architecture|codebase|system|pipeline|site))/i.test(p + " " + r);
    const hasTechnicalMetrics = /\b\d+\s*(?:mb|gb|kb|ms|hz|khz|mhz|bit)\b/i.test(p + " " + r);

    if (isProblemOutcome || (isProjectConfiguration && hasTechnicalMetrics)) {
        if (!isMemoryInquiry(p)) {
          const isQuestionOrDirective = /^(?:why|how|what|when|where|which|who|investigate|analyze|explain|debug|check|find|fix|resolve|can\s+you|could\s+you|please)\b/i.test(p) || p.endsWith("?");
          const isAssistantAgreement = /^(?:yes|correct|indeed|agreed|right|yep|exactly)\b/i.test(r);
          const summaryCandidate = isQuestionOrDirective ? r : (isAssistantAgreement ? p : (r.length >= 30 ? r : p));
          const cleanSummary = cleanSummarySentence(summaryCandidate);
          if (cleanSummary.length >= 20) {
          return {
            meaningful: true,
            type: isProblemOutcome ? "episode" : "note",
            level: "L1",
            summary: cleanSummary,
            problem: p,
            findings: [cleanSummary],
          };
        }
      }
    }
  }

  return { meaningful: false };
}

export async function persistConversationalMemory(
  classification: ConversationalClassification,
  envSource: EnvSource,
  ctx: PiCtx,
): Promise<number | null> {
  try {
    const env = await envSource(ctx);
    if (!env.cfg.qmapBin || !classification.meaningful || !classification.summary) return null;

    const sessionManager = ctx.sessionManager ||
      ((ctx as unknown as Record<string, unknown>)?.sessionManager as
        | { getSessionId?: () => string; getSessionFile?: () => string; getLeafId?: () => string; id?: string; sessionId?: string }
        | undefined);
    const sessionId = sessionManager?.getSessionId?.() ||
      sessionManager?.id ||
      sessionManager?.sessionId ||
      ctx.sessionId ||
      ((ctx as unknown as Record<string, unknown>)?.sessionId as string) ||
      (typeof process !== "undefined" ? process.env?.["PI_SESSION_ID"] : undefined) ||
      (typeof Deno !== "undefined" ? Deno.env.get("PI_SESSION_ID") : undefined);
    const sessionFile = sessionManager?.getSessionFile?.() ||
      (typeof process !== "undefined" ? process.env?.["PI_SESSION_FILE"] : undefined) ||
      (typeof Deno !== "undefined" ? Deno.env.get("PI_SESSION_FILE") : undefined);
    const leafId = sessionManager?.getLeafId?.();

    const episode: EpisodeRecord = {
      problem: classification.problem,
      findings: classification.findings,
      outcome: "success",
      sessionId: sessionId ?? undefined,
      sessionFile: sessionFile ?? undefined,
      leafId: leafId ?? undefined,
    };

    const envelope: MemoryEnvelope = {
      level: classification.level ?? "L1",
      type: classification.type ?? "note",
      lifecycle: "observed",
      summary: classification.summary,
      evidence: classification.evidence,
      episode,
    };

    const date = env.nowProvider().toISOString().slice(0, 10);
    const payload = encodePayload(date, envelope);
    const cwd = memDir(env);
    const axes = axisPath(env);
    const embed = sepalConfigured(env.cfg);
    const fspec = filespecFor(cwd, embed);
    const eEnv = embedEnv(env.cfg);

    debugLog("persistence", "Attempting conversational memory store", {
      fact: classification.summary,
      level: classification.level ?? "L1",
      type: classification.type ?? "note",
      memDir: cwd,
      filespec: fspec,
      semanticSearch: embed,
    });

    return await withLock(cwd, async () => {
      const list = await env.runner.run(buildListInvocation(env.cfg.qmapBin, fspec, axes, cwd, eEnv));
      const ref = nextRef(parseBareRefs(list.stdout));
      const res = await env.runner.run(buildStoreInvocation(env.cfg.qmapBin, fspec, ref, payload, axes, cwd, eEnv));
      debugLog("persistence", "Executed corm store", {
        ref,
        exitCode: res.code,
        stdout: res.stdout,
        stderr: res.stderr,
      });

      traceEvent("memory_write", res.code === 0 ? "ok" : "error", {
        targetFilespec: fspec,
        proposedSummary: classification.summary,
        exitStatus: res.code,
        assignedRef: res.code === 0 ? ref : null,
        stderr: res.stderr || null,
        persisted: res.code === 0,
      }, { sessionId });

      if (res.code === 0) {
        debugLog("persistence", "Successfully allocated memory reference", { ref });
        return ref;
      }
      debugLog("persistence", "CORM store mutation returned non-zero code", {
        code: res.code,
        stderr: res.stderr,
      });
      return null;
    });
  } catch (err) {
    debugLog("persistence", "Conversational memory persistence failed with exception", {
      error: err instanceof Error ? err.message : String(err),
      stack: err instanceof Error ? err.stack : undefined,
    });
    return null;
  }
}

export async function enrichActiveMemory(
  activeRef: number,
  newFact: string,
  envSource: EnvSource,
  ctx: PiCtx,
): Promise<boolean> {
  try {
    const env = await envSource(ctx);
    const cwd = memDir(env);
    const axes = axisPath(env);
    const embed = sepalConfigured(env.cfg);
    const fspec = filespecFor(cwd, embed);
    const eEnv = embedEnv(env.cfg);

    return await withLock(cwd, async () => {
      const getRes = await env.runner.run(buildGetInvocation(env.cfg.qmapBin, fspec, activeRef, axes, cwd, eEnv));
      if (getRes.code !== 0 || !getRes.stdout.trim()) return false;

      const decoded = decodePayload(getRes.stdout.trim());
      if (!decoded.envelope.episode) {
        decoded.envelope.episode = {
          problem: decoded.envelope.summary,
          findings: [decoded.envelope.summary],
          outcome: "success",
        };
      }
      const findings = decoded.envelope.episode.findings ?? [decoded.envelope.summary];
      if (!findings.includes(newFact)) {
        findings.push(newFact);
      }
      decoded.envelope.episode.findings = findings;

      const payload = encodePayload(decoded.date, decoded.envelope);
      const storeRes = await env.runner.run(buildStoreInvocation(env.cfg.qmapBin, fspec, activeRef, payload, axes, cwd, eEnv));
      debugLog("persistence", "Enriched existing active memory with follow-up", {
        ref: activeRef,
        newFact,
        totalFindings: findings.length,
        exitCode: storeRes.code,
      });
      return storeRes.code === 0;
    });
  } catch (err) {
    debugLog("persistence", "Failed to enrich active memory", {
      ref: activeRef,
      error: err instanceof Error ? err.message : String(err),
    });
    return false;
  }
}

export async function handleMemoryUpdate(
  decision: JudgeDecision,
  prompt: string,
  envSource: EnvSource,
  ctx: PiCtx,
  supportingEvidenceRef?: number,
): Promise<string | null> {
  if (!decision.summary) return null;
  try {
    const env = await envSource(ctx);
    const cwd = memDir(env);
    const axes = axisPath(env);
    const embed = sepalConfigured(env.cfg);
    const fspec = filespecFor(cwd, embed);
    const eEnv = embedEnv(env.cfg);

    const sessionManager = ctx.sessionManager ||
      ((ctx as unknown as Record<string, unknown>)?.sessionManager as
        | { getSessionId?: () => string; getSessionFile?: () => string; getLeafId?: () => string; id?: string; sessionId?: string }
        | undefined);
    const sessionId = sessionManager?.getSessionId?.() ||
      sessionManager?.id ||
      sessionManager?.sessionId ||
      ctx.sessionId ||
      ((ctx as unknown as Record<string, unknown>)?.sessionId as string) ||
      (typeof process !== "undefined" ? process.env?.["PI_SESSION_ID"] : undefined) ||
      (typeof Deno !== "undefined" ? Deno.env.get("PI_SESSION_ID") : undefined);
    const sessionFile = sessionManager?.getSessionFile?.() ||
      (typeof process !== "undefined" ? process.env?.["PI_SESSION_FILE"] : undefined) ||
      (typeof Deno !== "undefined" ? Deno.env.get("PI_SESSION_FILE") : undefined);
    const leafId = sessionManager?.getLeafId?.();

    let targetRefNum: number | undefined = undefined;
    if (decision.targetRef !== undefined) {
      const parsed = typeof decision.targetRef === "number"
        ? decision.targetRef
        : parseInt(String(decision.targetRef).replace(/\D/g, ""), 10);
      if (!isNaN(parsed) && parsed > 0) targetRefNum = parsed;
    }

    // 2. Same-session active memory preference
    if (
      targetRefNum === undefined &&
      activeConversationTurn.activeMemoryRef !== undefined &&
      activeConversationTurn.activeSessionId === sessionId
    ) {
      targetRefNum = activeConversationTurn.activeMemoryRef;
    }

    // 3. Search for genuinely related active candidate if targetRef is still unset
    if (targetRefNum === undefined) {
      try {
        const scanRes = await executeHybridScan(env, { topic: prompt, limit: 3, embed });
        const activeCandidates = scanRes.records.filter((r) => {
          const lc = r.envelope?.lifecycle ?? r.lifecycle;
          const lvl = r.envelope?.level ?? r.level;
          return lvl === "L1" && lc !== "superseded" && lc !== "contradicted";
        });
        if (activeCandidates.length > 0) {
          targetRefNum = activeCandidates[0].ref;
        }
      } catch {
        // scan fallback
      }
    }

    return await withLock(cwd, async () => {
      let priorFindings: string[] = [];
      let priorEvidence: number[] = [];
      let priorSummary = "";
      let validTarget = false;
      let targetPayloadToUpdate: { date: string; envelope: MemoryEnvelope } | null = null;

      if (targetRefNum !== undefined) {
        const getRes = await env.runner.run(buildGetInvocation(env.cfg.qmapBin, fspec, targetRefNum, axes, cwd, eEnv));
        if (getRes.code === 0 && getRes.stdout.trim()) {
          try {
            const oldDecoded = decodePayload(getRes.stdout.trim());
            // Reject missing, contradicted or superseded targets
            if (oldDecoded.envelope.lifecycle !== "superseded" && oldDecoded.envelope.lifecycle !== "contradicted") {
              validTarget = true;
              targetPayloadToUpdate = oldDecoded;
              priorSummary = oldDecoded.envelope.summary;
              if (oldDecoded.envelope.episode?.findings) {
                priorFindings = oldDecoded.envelope.episode.findings;
              } else if (oldDecoded.envelope.summary) {
                priorFindings = [oldDecoded.envelope.summary];
              }
              if (oldDecoded.envelope.evidence) {
                priorEvidence = oldDecoded.envelope.evidence;
              }
            }
          } catch {
            validTarget = false;
          }
        }
      }

      const rawFindings = [...priorFindings, ...(decision.findings ?? (decision.summary ? [decision.summary] : []))];
      const mergedFindings: string[] = Array.from(new Set(rawFindings.filter((f): f is string => typeof f === "string" && f.length > 0)));

      const rawEvidence = [...priorEvidence, ...(supportingEvidenceRef !== undefined ? [supportingEvidenceRef] : [])];
      const mergedEvidence: number[] = Array.from(new Set(rawEvidence));

      const decisionSummary = decision.summary ?? "Conversational update";
      let updatedSummary = decisionSummary;
      if (validTarget && priorSummary) {
        const pLower = priorSummary.toLowerCase();
        const dLower = decisionSummary.toLowerCase();
        if (pLower.includes(dLower)) {
          updatedSummary = priorSummary;
        } else if (dLower.includes(pLower)) {
          updatedSummary = decisionSummary;
        } else {
          updatedSummary = `${priorSummary} — ${decisionSummary}`;
          if (updatedSummary.length > 250) {
            updatedSummary = updatedSummary.slice(0, 247) + "...";
          }
        }
      }

      const list = await env.runner.run(buildListInvocation(env.cfg.qmapBin, fspec, axes, cwd, eEnv));
      const allRefs = parseBareRefs(list.stdout);
      const newRef = nextRef(allRefs);

      const episode: EpisodeRecord = {
        problem: prompt,
        findings: mergedFindings,
        outcome: "success",
        sessionId: sessionId ?? targetPayloadToUpdate?.envelope.episode?.sessionId,
        sessionFile: sessionFile ?? targetPayloadToUpdate?.envelope.episode?.sessionFile,
        leafId: leafId ?? targetPayloadToUpdate?.envelope.episode?.leafId,
      };

      const newEnvelope: MemoryEnvelope = {
        level: "L1",
        type: decision.type ?? targetPayloadToUpdate?.envelope.type ?? "note",
        lifecycle: "observed",
        summary: updatedSummary,
        supersedes: validTarget && targetRefNum !== undefined ? targetRefNum : undefined,
        evidence: mergedEvidence.length > 0 ? mergedEvidence : undefined,
        episode,
      };

      const date = env.nowProvider().toISOString().slice(0, 10);
      const newPayload = encodePayload(date, newEnvelope);

      // Store the new successor record
      const res = await env.runner.run(buildStoreInvocation(env.cfg.qmapBin, fspec, newRef, newPayload, axes, cwd, eEnv));
      if (res.code !== 0) return null;

      // If valid target was superseded, update predecessor with bidirectional supersededBy link
      if (validTarget && targetRefNum !== undefined && targetPayloadToUpdate) {
        targetPayloadToUpdate.envelope.lifecycle = "superseded";
        targetPayloadToUpdate.envelope.supersededBy = newRef;
        const oldUpdatedPayload = encodePayload(targetPayloadToUpdate.date, targetPayloadToUpdate.envelope);
        await env.runner.run(buildStoreInvocation(env.cfg.qmapBin, fspec, targetRefNum, oldUpdatedPayload, axes, cwd, eEnv));
      }

      // Update active turn tracking to the new active ref
      activeConversationTurn.activeMemoryRef = newRef;
      activeConversationTurn.activeSessionId = sessionId;

      debugLog("persistence", "Executed memory update with successor management", {
        newRef,
        supersedes: validTarget ? targetRefNum : null,
        evidence: mergedEvidence,
        exitCode: res.code,
      });

      traceEvent("memory_write", "ok", {
        targetFilespec: fspec,
        proposedSummary: updatedSummary,
        exitStatus: 0,
        assignedRef: newRef,
        persisted: true,
        supersedes: validTarget ? targetRefNum : null,
      }, { sessionId });

      return String(newRef);
    });
  } catch (err) {
    debugLog("persistence", "Memory update persistence failed with exception", {
      error: err instanceof Error ? err.message : String(err),
      stack: err instanceof Error ? err.stack : undefined,
    });
    return null;
  }
}

export function installSessionHooks(pi: Pi, envSource?: EnvSource): void {
  pi.on("session_start", async (_event: unknown, ctxUnknown?: unknown) => {
    activeSessionBuffer.onSessionStart();
    activeConversationTurn.activeMemoryRef = undefined;
    activeConversationTurn.activeSessionId = undefined;
    if (envSource && ctxUnknown) {
      try {
        const ctx = ctxUnknown as PiCtx;
        const sessionId = extractSessionId(ctx);
        const env = await envSource(ctx);
        console.log(`[pi-mm] ${describeCapabilities(env.cfg)}`);
        const cwd = memDir(env);
        activeSessionBuffer.setJournalDir(cwd);
        const committedIds = new Set<string>();
        const embed = sepalConfigured(env.cfg);
        const fspec = filespecFor(cwd, embed);
        const axes = axisPath(env);

        debugLog("startup", "Session started", {
          memDir: cwd,
          filespec: fspec,
          axes: axes.split(":"),
          semanticSearch: embed,
          judgeMode: env.cfg.judgeMode,
          judgeUrl: env.cfg.judgeUrl,
          judgeModel: env.cfg.judgeModel,
          judgeFallback: env.cfg.judgeFallback,
        });

        traceEvent("extension_loaded", "ok", {
          extension: "pi-mm",
          version: "0.1.0",
          cwd: ctx.cwd,
          memDir: cwd,
          filespec: fspec,
          axisRoster: axes.split(":"),
          cormdRunning: isDaemonLockHeld(cwd),
          cormdSocket: `${cwd}/cormd.sock`,
          judgeMode: env.cfg.judgeMode,
          judgeUrl: env.cfg.judgeUrl,
          judgeModel: env.cfg.judgeModel,
          judgeFallback: env.cfg.judgeFallback,
          semanticSearch: embed,
          embedUrl: env.cfg.embedUrl,
          embedModel: env.cfg.embedModel,
        }, { sessionId });

        const list = await env.runner.run(buildListInvocation(env.cfg.qmapBin, fspec, axes, cwd));
        for (const r of parseBareRefs(list.stdout)) {
          const res = await env.runner.run(buildGetInvocation(env.cfg.qmapBin, fspec, r, axes, cwd));
          if (res.code === 0 && res.stdout.trim()) {
            const { envelope } = decodePayload(res.stdout.trim());
            for (const id of envelope.episode?.eventIds || []) committedIds.add(id);
          }
        }
        await activeSessionBuffer.restoreJournal(cwd, committedIds);
      } catch (err) {
        debugLog("startup", "session_start failed", {
          error: err instanceof Error ? err.message : String(err),
        });
      }
    }
  });

  pi.on("tool_result", async (eventUnknown: unknown) => {
    if (typeof eventUnknown !== "object" || eventUnknown === null) return;
    const event = eventUnknown as ToolResultEvent;
    const sig = summarizeToolEvent(event);
    if (sig) await activeSessionBuffer.recordAsync(sig);
  });

  pi.on("message_end", (eventUnknown: unknown) => {
    if (typeof eventUnknown !== "object" || eventUnknown === null) return;
    const event = eventUnknown as { message?: { role?: string; content?: unknown } };
    if (event.message?.role === "assistant") {
      const content = event.message.content;
      let text = "";
      if (typeof content === "string") {
        text = content.trim();
      } else if (Array.isArray(content)) {
        text = content
          .filter((p: unknown) => typeof p === "object" && p !== null && (p as { type?: string }).type === "text")
          .map((p: unknown) => (p as { text?: string }).text ?? "")
          .join("\n")
          .trim();
      }
      if (text) {
        activeConversationTurn.response = text;
        if (!activeConversationTurn.responses) activeConversationTurn.responses = [];
        activeConversationTurn.responses.push(text);
        debugLog("capture", "Assistant response collected", {
          responseLength: text.length,
          responsePreview: text.slice(0, 100),
          turnResponsesCount: activeConversationTurn.responses.length,
        });
        traceEvent("conversation_captured", "ok", {
          lifecycle: "message_end",
          assistantResponse: text,
        });
      }
    }
  });

  const onTurnEnd = async (_event: unknown, ctxUnknown?: unknown) => {
    if (!envSource || !ctxUnknown) return;
    const ctx = ctxUnknown as PiCtx;

    // Per-turn boundary: flush tool events as required, but do not clear
    // prompt or response across intermediate turns of an agent run.
    await flushSessionBuffer(envSource, ctx);
  };

  const onRunSettled = async (eventUnknown: unknown, ctxUnknown?: unknown) => {
    if (!envSource || !ctxUnknown) return;
    const ctx = ctxUnknown as PiCtx;
    const sessionId = extractSessionId(ctx);
    const isAborted = Boolean((eventUnknown as { aborted?: boolean })?.aborted);

    debugLog("capture", "agent_settled triggered", {
      hasPrompt: Boolean(activeConversationTurn.prompt),
      promptPreview: activeConversationTurn.prompt ? activeConversationTurn.prompt.slice(0, 100) : undefined,
      hasResponse: Boolean(activeConversationTurn.response || activeConversationTurn.responses?.length),
      aborted: isAborted,
    });

    traceEvent("conversation_captured", isAborted ? "error" : "ok", {
      lifecycle: "agent_settled",
      settled: true,
      aborted: isAborted,
      prompt: activeConversationTurn.prompt,
      response: activeConversationTurn.response ?? activeConversationTurn.responses?.join("\n"),
    }, { sessionId });

    // 1. Flush any remaining tool-call events
    await flushSessionBuffer(envSource, ctx);

    // 2. Finalize and persist conversational memory strictly after the full agent run settles
    if (activeConversationTurn.prompt && !isAborted) {
      const prompt = activeConversationTurn.prompt;
      // Prefer the final assistant response (which contains the conclusion), falling back
      // to combined responses if individual turns held parts of the answer.
      const response = activeConversationTurn.response ??
        (activeConversationTurn.responses ? activeConversationTurn.responses.join("\n") : "");

      // Thread context for judge
      const threadContext = (activeConversationTurn.previousUserPrompt && activeConversationTurn.activeSessionId === sessionId)
        ? `Topic: ${activeConversationTurn.previousUserPrompt}\nPreceding Assistant Question: ${(activeConversationTurn.previousAssistantResponse || "").slice(0, 150)}`
        : undefined;

      // Update previous turn tracking for the active session
      activeConversationTurn.previousUserPrompt = prompt;
      activeConversationTurn.previousAssistantResponse = response;
      activeConversationTurn.prompt = undefined;
      activeConversationTurn.response = undefined;
      activeConversationTurn.responses = undefined;

      // Filter obvious greetings, memory inquiries, farewells, and explicit forget directives
      if (
        isForgetDirective(prompt) ||
        isTrivialGreeting(prompt) ||
        isMemoryInquiry(prompt) ||
        isTrivialFarewell(prompt)
      ) {
        debugLog("capture", "Skipping conversational capture for filtered prompt", { promptPreview: prompt.slice(0, 60) });
        return;
      }

      // STAGE 1: L0 Experience Capture
      const l0Classification: ConversationalClassification = {
        meaningful: true,
        type: "note",
        level: "L0",
        summary: prompt.trim().slice(0, 200),
        problem: prompt,
        findings: [prompt.trim()],
      };
      const l0Ref = await persistConversationalMemory(l0Classification, envSource, ctx);
      debugLog("capture", "Captured L0 experience record", { l0Ref, summary: l0Classification.summary });

      // STAGE 2: L1 Episodic Memory Evaluation via Judge (with Thread Context)
      const env = await envSource(ctx);
      const judgeStart = Date.now();
      debugLog("judgement", "Evaluating conversational turn with judge", {
        promptPreview: prompt.slice(0, 100),
        responseLength: response.length,
        hasThreadContext: Boolean(threadContext),
        judgeMode: env.cfg.judgeMode,
        judgeUrl: env.cfg.judgeUrl,
        judgeModel: env.cfg.judgeModel,
      });

      const judgeRes = await evaluateTurnWithJudge(prompt, response, env.cfg, globalThis.fetch, threadContext);
      const judgeDurationMs = Date.now() - judgeStart;

      debugLog("judgement", "Judge evaluated turn", {
        action: judgeRes.decision.action,
        source: judgeRes.source,
        durationMs: judgeDurationMs,
        degraded: Boolean(judgeRes.degraded),
        summary: judgeRes.decision.summary,
        reason: judgeRes.decision.reason,
      });

      traceEvent("judge_request", judgeRes.decision.action === "skip" ? "skip" : "ok", {
        endpoint: env.cfg.judgeUrl || "local-heuristic",
        model: env.cfg.judgeModel || "none",
        status: judgeRes.degraded ? "degraded" : (judgeRes.source === "llm" ? "http-200" : "heuristic"),
        elapsedMs: judgeDurationMs,
        action: judgeRes.decision.action,
        summary: judgeRes.decision.summary,
        failureReason: judgeRes.decision.reason || null,
      }, { sessionId });

      if (judgeRes.decision.action === "remember" && judgeRes.decision.summary) {
        const classification: ConversationalClassification = {
          meaningful: true,
          type: judgeRes.decision.type ?? "note",
          level: "L1",
          summary: judgeRes.decision.summary,
          problem: prompt,
          findings: judgeRes.decision.findings ?? [judgeRes.decision.summary],
          evidence: l0Ref !== null ? [l0Ref] : undefined,
        };

        if (
          activeConversationTurn.activeMemoryRef !== undefined &&
          activeConversationTurn.activeSessionId === sessionId &&
          classification.type === "note"
        ) {
          const updatedRef = await handleMemoryUpdate(
            {
              action: "update",
              summary: judgeRes.decision.summary,
              type: classification.type,
              findings: classification.findings,
            },
            prompt,
            envSource,
            ctx,
            l0Ref !== null ? l0Ref : undefined,
          );
          if (!updatedRef) {
            const ref = await persistConversationalMemory(classification, envSource, ctx);
            if (ref) activeConversationTurn.activeMemoryRef = ref;
          }
        } else {
          const ref = await persistConversationalMemory(classification, envSource, ctx);
          if (ref) {
            activeConversationTurn.activeMemoryRef = ref;
            activeConversationTurn.activeSessionId = sessionId;
          }
        }
      } else if (judgeRes.decision.action === "update" && judgeRes.decision.summary) {
        await handleMemoryUpdate(judgeRes.decision, prompt, envSource, ctx, l0Ref !== null ? l0Ref : undefined);
      }

      // Incremental consolidation: run bounded, idempotent auto-consolidation
      try {
        await runAutoConsolidation(env, ctx);
      } catch (err) {
        debugLog("consolidation", "Incremental consolidation error", {
          error: err instanceof Error ? err.message : String(err),
        });
      }
    }
  };

  pi.on("before_agent_start", async (eventUnknown: unknown, ctxUnknown?: unknown) => {
    const ctx = ctxUnknown as PiCtx | undefined;
    const sessionId = extractSessionId(ctx);
    if (typeof eventUnknown === "object" && eventUnknown !== null) {
      const event = eventUnknown as BeforeAgentStartEvent;
      if (event.messageType === "user" && typeof event.prompt === "string") {
        activeConversationTurn.prompt = event.prompt;
        activeConversationTurn.response = undefined;
        activeConversationTurn.responses = undefined;
        activeConversationTurn.timestamp = new Date().toISOString();
        debugLog("capture", "before_agent_start user prompt captured", {
          promptPreview: event.prompt.slice(0, 100),
          length: event.prompt.length,
        });
        traceEvent("conversation_captured", "ok", {
          lifecycle: "before_agent_start",
          messageType: event.messageType,
          userPrompt: event.prompt,
        }, { sessionId });
      }
    }
    if (!envSource || !ctxUnknown) return;
    const event = eventUnknown as BeforeAgentStartEvent;
    return await handleBeforeAgentStart(event, ctxUnknown as PiCtx, envSource);
  });

  pi.on("turn_end", onTurnEnd);
  pi.on("agent_settled", onRunSettled);
  pi.on("session_before_compact", onRunSettled);
}
