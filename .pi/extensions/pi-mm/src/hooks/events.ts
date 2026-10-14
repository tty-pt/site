import type { EnvSource } from "../tools/index.ts";
import { axisPath, memDir, sepalConfigured } from "../tools/index.ts";
import { filespecFor, buildStoreInvocation, buildListInvocation, embedEnv } from "../qmap.ts";
import { parseBareRefs, nextRef } from "../resolve.ts";
import { encodePayload } from "../payload.ts";
import { withLock } from "../lock.ts";
import type { MemoryEnvelope, EpisodeRecord } from "../types.ts";
import { readFile, writeFile, unlink, mkdir } from "node:fs/promises";
import { join } from "node:path";

export interface PiToolSpec {
  name: string;
  label: string;
  description: string;
  parameters: unknown;
  execute: (
    toolCallId: string,
    params: Record<string, unknown>,
    signal: AbortSignal | undefined,
    onUpdate: unknown,
    ctx: PiCtx,
  ) => Promise<PiAgentToolResult>;
}

export interface PiAgentToolResult {
  content: Array<{ type: string; text?: string }>;
  details?: unknown;
}

export interface PiExecResult {
  stdout: string;
  stderr: string;
  code: number;
}

export interface PiCtx {
  cwd: string;
  hasUI: boolean;
  mode: string;
  ui?: unknown;
}

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
  exec(
    command: string,
    args: string[],
    options?: { cwd?: string; env?: Record<string, string> },
  ): Promise<PiExecResult>;
}

export class SessionEventBuffer {
  private events: SignificantEvent[] = [];
  private maxEvents = 50;
  private lastFlushedCount = 0;
  private lastFlushedFingerprint = "";
  private flushedEventIds = new Set<string>();
  public lastError: string | null = null;

  public clear(): void {
    this.events = [];
    this.lastFlushedCount = 0;
    this.lastFlushedFingerprint = "";
    this.flushedEventIds.clear();
    this.lastError = null;
  }

  public onSessionStart(): void {
    if (this.lastFlushedCount > 0) {
      this.events = this.events.slice(this.lastFlushedCount);
      this.lastFlushedCount = 0;
    }
  }

  public record(event: SignificantEvent): void {
    if (this.flushedEventIds.has(event.id) || this.events.some((e) => e.id === event.id)) return;
    if (this.events.length >= this.maxEvents) {
      this.events.shift();
      if (this.lastFlushedCount > 0) this.lastFlushedCount--;
    }
    this.events.push(event);
  }

  public getEvents(): readonly SignificantEvent[] { return this.events; }
  public hasUnflushed(): boolean { return this.events.length > this.lastFlushedCount; }
  public getUnflushed(): SignificantEvent[] { return this.events.slice(this.lastFlushedCount); }

  public markFlushed(fingerprint: string, ids: string[]): void {
    this.lastFlushedCount = this.events.length;
    this.lastFlushedFingerprint = fingerprint;
    for (const id of ids) this.flushedEventIds.add(id);
    this.lastError = null;
  }

  public getLastFingerprint(): string { return this.lastFlushedFingerprint; }

  public getApproaches(): Array<{ approach: string; outcome: "worked" | "failed" | "abandoned"; reason?: string }> {
    return this.events.filter((e) => e.approach && e.outcome).map((e) => ({
      approach: e.approach!,
      outcome: e.outcome!,
      reason: e.summary,
    }));
  }

  public getDiffs(): string[] {
    const diffs: string[] = [];
    for (const e of this.events) if (e.diffs) diffs.push(...e.diffs);
    return diffs;
  }

  public async saveJournal(dir: string): Promise<void> {
    const unflushed = this.getUnflushed();
    const journalFile = join(dir, ".pending_events.json");
    try {
      await mkdir(dir, { recursive: true });
      if (unflushed.length === 0) {
        await unlink(journalFile).catch(() => {});
      } else {
        await writeFile(journalFile, JSON.stringify(unflushed));
      }
    } catch { /* best-effort journal */ }
  }

  public async restoreJournal(dir: string): Promise<number> {
    const journalFile = join(dir, ".pending_events.json");
    try {
      const data = await readFile(journalFile, "utf-8");
      const loaded: SignificantEvent[] = JSON.parse(data);
      let count = 0;
      for (const ev of loaded) {
        if (!this.flushedEventIds.has(ev.id) && !this.events.some((e) => e.id === ev.id)) {
          this.events.push(ev);
          count++;
        }
      }
      return count;
    } catch { return 0; }
  }
}

export const activeSessionBuffer = new SessionEventBuffer();

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

  const failed = isErr || /FAIL|failed|error|fatal|panic/i.test(text);
  const passed = !failed && (/PASS|passed|ok|succeeded/i.test(text) || !isErr);

  const outcome: "worked" | "failed" = passed ? "worked" : "failed";
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
    approach: `Executed ${event.toolName}: ${firstLine.slice(0, 80)}`,
    outcome,
    diffs,
    environment: envInfo,
    toolArgs,
  };
}

export async function flushSessionBuffer(envSource: EnvSource, ctx: PiCtx): Promise<number | null> {
  if (!activeSessionBuffer.hasUnflushed()) return null;
  const unflushed = activeSessionBuffer.getUnflushed();
  if (unflushed.length === 0) return null;

  const ids = unflushed.map((e) => e.id);
  const fingerprint = unflushed.map((e) => `${e.id}:${e.outcome}`).join(";");
  if (fingerprint === activeSessionBuffer.getLastFingerprint()) return null;

  try {
    const env = await envSource(ctx);
    if (!env.cfg.qmapBin) {
      activeSessionBuffer.lastError = "qmap-not-found";
      return null;
    }

    const firstEvent = unflushed[0];
    const approaches = activeSessionBuffer.getApproaches();
    const observations = unflushed.map((e) => e.summary);
    const diffs = activeSessionBuffer.getDiffs();
    const hasFail = unflushed.some((e) => e.isError);

    const episode: EpisodeRecord = {
      problem: firstEvent.summary,
      request: `Session event: ${firstEvent.summary}`,
      approaches,
      observations,
      diffs: diffs.length > 0 ? diffs : undefined,
      environment: firstEvent.environment,
      outcome: hasFail ? "failure" : "success",
    };

    const envelope: MemoryEnvelope = {
      level: "L0",
      type: "episode",
      lifecycle: "observed",
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
        activeSessionBuffer.markFlushed(fingerprint, ids);
        await activeSessionBuffer.saveJournal(cwd);
        return ref;
      }
      activeSessionBuffer.lastError = `store-failed-code-${res.code}: ${res.stderr}`;
      await activeSessionBuffer.saveJournal(cwd);
      return null;
    });
  } catch (err) {
    activeSessionBuffer.lastError = err instanceof Error ? err.message : String(err);
    return null;
  }
}

export function installSessionHooks(pi: Pi, envSource?: EnvSource): void {
  pi.on("session_start", async (_event: unknown, ctxUnknown?: unknown) => {
    activeSessionBuffer.onSessionStart();
    if (envSource && ctxUnknown) {
      try {
        const env = await envSource(ctxUnknown as PiCtx);
        await activeSessionBuffer.restoreJournal(memDir(env));
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
