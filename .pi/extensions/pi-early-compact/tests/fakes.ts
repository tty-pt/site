import type { EarlyCompactConfig } from "../src/config.ts";
import type { ContextUsage, PressureReport } from "../src/policy.ts";
import type { UltraCtx, UltraPi } from "../src/trigger.ts";

export function check(cond: boolean, msg: string): void {
  if (!cond) throw new Error(msg);
}

export interface FakePi extends UltraPi {
  handlers: Record<string, Array<(e: any, ctx: UltraCtx) => any>>;
  commands: Array<{ name: string; def: any }>;
  sentMessages: Array<{ message: any; options?: any }>;
  registerCommand(name: string, def: any): void;
  sendMessage(message: any, options?: any): void;
  emit(event: string, data: any, ctx: UltraCtx): Promise<any>;
}

export function makeFakePi(): FakePi {
  const handlers: Record<string, Array<(e: any, ctx: UltraCtx) => any>> = {};
  const commands: Array<{ name: string; def: any }> = [];
  const sentMessages: Array<{ message: any; options?: any }> = [];
  const pi: FakePi = {
    handlers,
    commands,
    sentMessages,
    on(event: string, handler: (e: any, ctx: UltraCtx) => any) {
      if (!handlers[event]) handlers[event] = [];
      handlers[event].push(handler);
    },
    registerCommand(name: string, def: any) {
      commands.push({ name, def });
    },
    sendMessage(message: any, options?: any) {
      sentMessages.push({ message, options });
    },
    async emit(event: string, data: any, ctx: UltraCtx) {
      const list = handlers[event] ?? [];
      let result: any;
      for (const h of list) {
        result = await h(data, ctx);
      }
      return result;
    },
  };
  return pi;
}

export interface FakeCtxOpts {
  tokens?: number | null;
  contextWindow?: number;
  hasUI?: boolean;
  compact?: boolean;
  onCompact?: () => void;
  onCompactError?: () => Error | null;
  signal?: AbortSignal;
  pending?: boolean;
}

export function makeCtx(opts: FakeCtxOpts = {}): UltraCtx & {
  ui: { notify: (m: string, t?: string) => void; setStatus: (k: string, v: string | undefined) => void; theme: { fg: (c: string, t: string) => string } };
  notifyCalls: Array<{ msg: string; type?: string }>;
  statusCalls: Array<{ key: string; text: string | undefined }>;
  compactCalls: number;
  usage: ContextUsage;
  signal: AbortSignal | undefined;
  pendingCompact: { onComplete?: () => void; onError?: (e: Error) => void } | undefined;
  resolveCompact(error?: Error | null): void;
} {
  const notifyCalls: Array<{ msg: string; type?: string }> = [];
  const statusCalls: Array<{ key: string; text: string | undefined }> = [];
  const usage: ContextUsage = {
    tokens: opts.tokens !== undefined ? opts.tokens : 50_000,
    contextWindow: opts.contextWindow ?? 200_000,
  };
  const fake = {
    hasUI: opts.hasUI ?? true,
    mode: "tui",
    notifyCalls,
    statusCalls,
    compactCalls: 0,
    usage,
    signal: "signal" in opts ? opts.signal : new AbortController().signal,
    pendingCompact: undefined as { onComplete?: () => void; onError?: (e: Error) => void } | undefined,
    getContextUsage: () => usage,
    ui: {
      notify: (msg: string, type?: string) => notifyCalls.push({ msg, type }),
      setStatus: (key: string, text: string | undefined) => statusCalls.push({ key, text }),
      theme: { fg: (color: string, text: string) => text },
    },
    resolveCompact: (error?: Error | null) => {
      const cd = fake.pendingCompact;
      if (!cd) return;
      fake.pendingCompact = undefined;
      if (error) cd.onError?.(error);
      else cd.onComplete?.();
    },
    compact: opts.compact === false
      ? undefined
      : (cd: { onComplete?: () => void; onError?: (e: Error) => void }) => {
          fake.compactCalls++;
          if (opts.pending) {
            fake.pendingCompact = cd;
            return;
          }
          if (opts.onCompactError) {
            const err = opts.onCompactError();
            if (err) cd.onError?.(err);
            else cd.onComplete?.();
          } else {
            cd.onComplete?.();
          }
        },
  };
  const ctx = fake as unknown as UltraCtx & {
    ui: { notify: (m: string, t?: string) => void; setStatus: (k: string, v: string | undefined) => void; theme: { fg: (c: string, t: string) => string } };
    notifyCalls: Array<{ msg: string; type?: string }>;
    statusCalls: Array<{ key: string; text: string | undefined }>;
    compactCalls: number;
    usage: ContextUsage;
    signal: AbortSignal | undefined;
    pendingCompact: { onComplete?: () => void; onError?: (e: Error) => void } | undefined;
    resolveCompact(error?: Error | null): void;
  };
  return ctx;
}

export function makeConfig(overrides: Partial<EarlyCompactConfig> = {}): EarlyCompactConfig {
  return {
    enabled: true,
    adaptive: true,
    midRunCompact: true,
    continueAfterCompact: true,
    thresholdPct: null,
    thresholdTokens: null,
    warningPct: null,
    warningTokens: null,
    warningMarginTokens: null,
    ...overrides,
  };
}

export function jsonConfig(overrides: Partial<EarlyCompactConfig> = {}): string {
  const cfg = makeConfig(overrides);
  return JSON.stringify({
    enabled: cfg.enabled,
    adaptive: cfg.adaptive,
    midRunCompact: cfg.midRunCompact,
    continueAfterCompact: cfg.continueAfterCompact,
    ...(cfg.thresholdPct !== null ? { thresholdPct: cfg.thresholdPct } : {}),
    ...(cfg.thresholdTokens !== null ? { thresholdTokens: cfg.thresholdTokens } : {}),
    ...(cfg.warningPct !== null ? { warningPct: cfg.warningPct } : {}),
    ...(cfg.warningTokens !== null ? { warningTokens: cfg.warningTokens } : {}),
    ...(cfg.warningMarginTokens !== null ? { warningMarginTokens: cfg.warningMarginTokens } : {}),
  });
}

export interface PendingResolveCap {
  onComplete?: () => void;
  onError?: (e: Error) => void;
}

export async function settle(): Promise<void> {
  await new Promise((r) => setTimeout(r, 0));
}

export type { EarlyCompactConfig, ContextUsage, PressureReport, UltraCtx, UltraPi };