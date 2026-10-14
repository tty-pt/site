import type { EnvSource } from "../tools/index.ts";
import { axisPath, memDir, sepalConfigured } from "../tools/index.ts";
import { filespecFor, buildStoreInvocation, buildListInvocation, buildGetInvocation, embedEnv } from "../qmap.ts";
import { parseBareRefs, nextRef } from "../resolve.ts";
import { encodePayload, decodePayload } from "../payload.ts";
import { withLock } from "../lock.ts";
import type { MemoryEnvelope, EpisodeRecord } from "../types.ts";
import { readFile, writeFile, unlink, mkdir, rename } from "node:fs/promises";
import { join } from "node:path";

export interface PiToolSpec {
  name: string;
  label: string;
  description: string;
  parameters: unknown;
  execute: (toolCallId: string, params: Record<string, unknown>, signal: AbortSignal | undefined, onUpdate: unknown, ctx: PiCtx) => Promise<PiAgentToolResult>;
}

export interface PiAgentToolResult { content: Array<{ type: string; text?: string }>; details?: unknown; }
export interface PiExecResult { stdout: string; stderr: string; code: number; }
export interface PiCtx { cwd: string; hasUI: boolean; mode: string; ui?: unknown; }

export interface ToolResultEvent {
  toolCallId?: string;
  toolName: string;
  input?: Record<string, unknown>;
  args?: Record<string, unknown>;
  content?: Array<{ type: string; text?: string }>;
  isError?: boolean;
  details?: unknown;
}

export interface SignificantEvent {
  id: string;
  timestamp: string;
  toolName: string;
  summary: string;
  isError: boolean;
  approach?: string;
  outcome?: "worked" | "failed" | "abandoned";
  diffs?: string[];
  environment?: Record<string, string>;
  toolArgs?: Record<string, unknown>;
}

export interface Pi {
  on(event: string, handler: (...args: unknown[]) => unknown): void;
  registerTool(tool: PiToolSpec): void;
  exec(command: string, args: string[], options?: { cwd?: string; env?: Record<string, string> }): Promise<PiExecResult>;
}

export class SessionEventBuffer {
  private events: SignificantEvent[] = [];
  private inFlightBatch: SignificantEvent[] | null = null;
  private maxEvents = 50;
  private flushedEventIds = new Set<string>();
  private journalDir: string | null = null;
  private journalQueue: Promise<void> = Promise.resolve();
  private isFlushing = false;
  public lastError: string | null = null;

  public setJournalDir(dir: string): void { this.journalDir = dir; }
  public clear(): void {
    this.events = []; this.inFlightBatch = null; this.flushedEventIds.clear(); this.lastError = null; this.isFlushing = false;
  }
  public onSessionStart(): void {
    this.events = this.events.filter((e) => !this.flushedEventIds.has(e.id));
    if (!this.isFlushing) this.inFlightBatch = null;
  }
  public record(event: SignificantEvent): void {
    if (this.flushedEventIds.has(event.id) || this.events.some((e) => e.id === event.id)) return;
    if (this.events.length >= this.maxEvents) this.events.shift();
    this.events.push(event);
    if (this.journalDir) void this.saveJournal(this.journalDir);
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
    for (const id of ids) this.flushedEventIds.add(id);
    this.inFlightBatch = null;
    this.isFlushing = false;
    this.lastError = null;
    this.events = this.events.filter((e) => !this.flushedEventIds.has(e.id));
  }
  public markFlushed(_fingerprint: string, ids: string[]): void { this.commitFlush(ids); }
  public getApproaches(): Array<{ approach: string; outcome: "worked" | "failed" | "abandoned"; reason?: string }> {
    return extractBatchApproaches(this.getUnflushed());
  }
  public getDiffs(): string[] { return extractBatchDiffs(this.getUnflushed()); }

  public rollbackFlush(err?: string): void {
    this.inFlightBatch = null;
    this.isFlushing = false;
    if (err) this.lastError = err;
  }

  public async saveJournal(dir: string): Promise<void> {
    const unflushed = this.getUnflushed();
    const journalFile = join(dir, ".pending_events.json");
    const tmpJournal = `${journalFile}.tmp.${Date.now()}.${Math.random().toString(36).slice(2, 6)}`;

    this.journalQueue = this.journalQueue.catch(() => {}).then(async () => {
      try {
        await mkdir(dir, { recursive: true });
        if (unflushed.length === 0) {
          await unlink(journalFile).catch(() => {});
        } else {
          await writeFile(tmpJournal, JSON.stringify(unflushed));
          await rename(tmpJournal, journalFile);
        }
        if (this.lastError?.startsWith("journal-error:")) {
          this.lastError = null;
        }
      } catch (err: unknown) {
        await unlink(tmpJournal).catch(() => {});
        const msg = err instanceof Error ? err.message : String(err);
        this.lastError = `journal-error: ${msg}`;
        throw err;
      }
    });
    try { await this.journalQueue; } catch { /* recorded in this.lastError */ }
  }

  public async restoreJournal(dir: string, existingEventIds?: Set<string>): Promise<number> {
    const journalFile = join(dir, ".pending_events.json");
    try {
      const data = await readFile(journalFile, "utf-8");
      const loaded: SignificantEvent[] = JSON.parse(data);
      let count = 0;
      for (const ev of loaded) {
        if (existingEventIds?.has(ev.id)) {
          this.flushedEventIds.add(ev.id);
          continue;
        }
        if (!this.flushedEventIds.has(ev.id) && !this.events.some((e) => e.id === ev.id)) {
          this.events.push(ev);
          count++;
        }
      }
      await this.saveJournal(dir);
      return count;
    } catch { return 0; }
  }
}

export const activeSessionBuffer = new SessionEventBuffer();

export function extractBatchApproaches(batch: SignificantEvent[]): Array<{ approach: string; outcome: "worked" | "failed" | "abandoned"; reason?: string }> {
  return batch.filter((e) => e.approach && e.outcome).map((e) => ({
    approach: e.approach!,
    outcome: e.outcome!,
    reason: e.summary,
  }));
}

export function extractBatchDiffs(batch: SignificantEvent[]): string[] {
  const diffs: string[] = [];
  for (const e of batch) if (e.diffs) diffs.push(...e.diffs);
  return diffs;
}

function extractDiffs(text: string): string[] {
  if (!text.includes("@@ -") && !text.includes("--- a/") && !text.includes("diff --git")) {
    return [];
  }
  const lines = text.split("\n");
  const diffLines = lines.filter((l) => /^[+-][^+-]/.test(l) || /^@@\s+-\d+/.test(l)).slice(0, 40);
  return diffLines.length > 0 ? [diffLines.join("\n")] : [];
}

export function summarizeToolEvent(event: ToolResultEvent): SignificantEvent | null {
  const isErr = Boolean(event.isError);
  const text = event.content?.map((c) => c.text || "").join(" ").trim() || "";

  const isTest = /test|make|deno|cargo|npm/i.test(text) || event.toolName === "bash";
  const isCodeMod = /edit|write|patch/i.test(event.toolName);
  if (!isTest && !isErr && !isCodeMod) return null;

  const cmd = String(event.input?.["command"] || event.args?.["command"] || "");
  const hasErrorWord = /\b(?:FAIL|failed|error|fatal|panic)\b/i.test(text.replace(/0\s+failed/gi, ""));
  const failed = isErr || hasErrorWord;
  const isVerification = /test|check|verify|smoke/i.test(event.toolName) ||
                         /test|check|make|deno|cargo|npm/i.test(cmd) ||
                         /test|PASS|check-complexity|integration|ok\s+\d+|all green/i.test(text);
  const verifiedWorked = !failed && isVerification && (/PASS|passed|all green|ok\s+-|0 failed/i.test(text));

  const outcome: "worked" | "failed" | undefined = failed ? "failed" : verifiedWorked ? "worked" : undefined;
  const firstLine = text.split("\n")[0]?.slice(0, 160) || `Executed ${event.toolName}`;

  const id = event.toolCallId || `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
  const extracted = isCodeMod ? extractDiffs(text) : [];
  const diffs = extracted.length > 0 ? extracted : undefined;
  const toolArgs = event.input ? { ...event.input } : event.args ? { ...event.args } : undefined;

  const envInfo: Record<string, string> = { runtime: typeof Deno !== "undefined" ? "deno" : "node" };

  return {
    id,
    timestamp: new Date().toISOString(),
    toolName: event.toolName,
    summary: firstLine,
    isError: failed,
    approach: outcome ? `Executed ${event.toolName}: ${firstLine.slice(0, 80)}` : undefined,
    outcome,
    diffs,
    environment: envInfo,
    toolArgs,
  };
}

let activeFlushPromise: Promise<number | null> | null = null;

async function executeBatchFlush(
  batch: SignificantEvent[],
  envSource: EnvSource,
  ctx: PiCtx,
): Promise<number | null> {
  const ids = batch.map((e) => e.id);
  const firstEvent = batch[0];
  const approaches = extractBatchApproaches(batch);
  const observations = batch.map((e) => e.summary);
  const diffs = extractBatchDiffs(batch);
  const hasFail = batch.some((e) => e.isError);

  try {
    const env = await envSource(ctx);
    if (!env.cfg.qmapBin) {
      activeSessionBuffer.rollbackFlush("qmap-not-found");
      return null;
    }

    const episode: EpisodeRecord = {
      problem: firstEvent.summary,
      request: `Session event: ${firstEvent.summary}`,
      approaches: approaches.length > 0 ? approaches : undefined,
      observations,
      diffs: diffs.length > 0 ? diffs : undefined,
      environment: firstEvent.environment,
      outcome: hasFail ? "failure" : approaches.some((a) => a.outcome === "worked") ? "success" : "observed",
      eventIds: ids,
    };

    const envelope: MemoryEnvelope = {
      level: "L0",
      type: "episode",
      lifecycle: episode.outcome === "success" ? "supported" : "observed",
      summary: `Automated Episode: ${firstEvent.summary}`,
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
        await activeSessionBuffer.saveJournal(cwd);
        return ref;
      }
      activeSessionBuffer.rollbackFlush(`store-failed-code-${res.code}: ${res.stderr}`);
      await activeSessionBuffer.saveJournal(cwd);
      return null;
    });
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    activeSessionBuffer.rollbackFlush(msg);
    return null;
  }
}

export async function flushSessionBuffer(envSource: EnvSource, ctx: PiCtx): Promise<number | null> {
  while (activeFlushPromise) {
    await activeFlushPromise;
  }

  const batch = activeSessionBuffer.beginFlush();
  if (!batch || batch.length === 0) return null;

  activeFlushPromise = executeBatchFlush(batch, envSource, ctx);
  try {
    return await activeFlushPromise;
  } finally {
    activeFlushPromise = null;
  }
}

export function installSessionHooks(pi: Pi, envSource?: EnvSource): void {
  pi.on("session_start", async (_event: unknown, ctxUnknown?: unknown) => {
    activeSessionBuffer.onSessionStart();
    if (envSource && ctxUnknown) {
      try {
        const env = await envSource(ctxUnknown as PiCtx);
        const cwd = memDir(env);
        activeSessionBuffer.setJournalDir(cwd);

        const committedIds = new Set<string>();
        const embed = sepalConfigured(env.cfg);
        const fspec = filespecFor(cwd, embed);
        const axes = axisPath(env);
        const list = await env.runner.run(buildListInvocation(env.cfg.qmapBin, fspec, axes, cwd));
        for (const r of parseBareRefs(list.stdout)) {
          const res = await env.runner.run(buildGetInvocation(env.cfg.qmapBin, fspec, r, axes, cwd));
          if (res.code === 0 && res.stdout.trim()) {
            const { envelope } = decodePayload(res.stdout.trim());
            for (const id of envelope.episode?.eventIds || []) committedIds.add(id);
          }
        }
        await activeSessionBuffer.restoreJournal(cwd, committedIds);
      } catch { /* best-effort */ }
    }
  });

  pi.on("tool_result", (eventUnknown: unknown) => {
    if (typeof eventUnknown !== "object" || eventUnknown === null) return;
    const event = eventUnknown as ToolResultEvent;
    const sig = summarizeToolEvent(event);
    if (sig) activeSessionBuffer.record(sig);
  });

  const onSettle = async (_event: unknown, ctxUnknown?: unknown) => {
    if (!envSource || !ctxUnknown) return;
    await flushSessionBuffer(envSource, ctxUnknown as PiCtx);
  };

  pi.on("agent_settled", onSettle);
  pi.on("turn_end", onSettle);
  pi.on("session_before_compact", onSettle);
}
