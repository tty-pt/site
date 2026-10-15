import type { EnvSource, ToolEnv } from "../tools/index.ts";
import { executeHybridScan } from "../tools/scan.ts";
import type { FusedCandidate } from "../tools/ranking.ts";
import type { PiCtx } from "./events.ts";

export const DEFAULT_AUTO_RECALL_BUDGET = 1500;
export const DEFAULT_AUTO_RECALL_TIMEOUT_MS = 1500;

const TRIVIAL_PROMPT_RE = /^(hi|hello|hey|greetings|thanks|thank you|thx|ok|okay|k|yes|no|yep|nope|sure|good morning|good afternoon|good evening|cool|bye|goodbye|test|ping|pong)[!.?\s]*$/i;

export function shouldTriggerAutoRecall(prompt: string): boolean {
  const trimmed = prompt.trim();
  if (trimmed.length < 5) return false;
  if (TRIVIAL_PROMPT_RE.test(trimmed)) return false;
  return true;
}

export function formatCompactMemoryEntry(cand: FusedCandidate): string {
  const ref = cand.ref;
  const env = cand.envelope;
  const level = env?.level ?? cand.level ?? "L2";
  const lifecycle = env?.lifecycle ?? cand.lifecycle ?? "supported";
  const isHypothesis = lifecycle === "hypothesized";
  const scopeParts: string[] = [];
  if (env?.context?.project) scopeParts.push(env.context.project);
  if (env?.context?.subsystem) scopeParts.push(env.context.subsystem);
  const scopeStr = scopeParts.length > 0 ? `, ${scopeParts.join("/")}` : "";
  const lifecycleLabel = isHypothesis ? "[HYPOTHESIS]" : lifecycle;

  const summary = env?.summary ?? cand.summary ?? cand.record.slice(0, 100);
  let line = `- [Ref ${ref}] (${level}, ${lifecycleLabel}${scopeStr}): ${summary}`;

  if (env?.evidence && env.evidence.length > 0) {
    line += `\n  Evidence: cited refs [${env.evidence.join(", ")}]`;
  }
  return line;
}

export function buildInjectedContext(
  candidates: FusedCandidate[],
  budget = DEFAULT_AUTO_RECALL_BUDGET,
): string {
  // Exclude contradicted and superseded records from active context instructions
  const valid = candidates.filter((c) => {
    const lc = c.envelope?.lifecycle ?? c.lifecycle;
    return lc !== "contradicted" && lc !== "superseded";
  });
  if (valid.length === 0) return "";

  const header = "[Persistent Memory Context]\nRelevant prior experience retrieved from pi-mm:\n";
  let content = header;

  for (const cand of valid) {
    const entry = formatCompactMemoryEntry(cand) + "\n";
    if (content.length + entry.length > budget) {
      if (content === header) {
        content += entry.slice(0, budget - content.length - 4) + "...\n";
      }
      break;
    }
    content += entry;
  }

  return content.trim() === header.trim() ? "" : content.trim();
}

async function runScanWithTimeout(
  env: ToolEnv,
  topic: string,
  timeoutMs: number,
): Promise<FusedCandidate[]> {
  const startTime = Date.now();
  let timerId: ReturnType<typeof setTimeout> | null = null;

  // Allocate ~67% of total budget to semantic hybrid, leaving remainder for lexical fallback
  const semanticTimeout = Math.min(1000, Math.max(100, Math.floor(timeoutMs * 0.67)));

  try {
    const hybridPromise = executeHybridScan(env, { topic, limit: 5, embed: true })
      .then((res) => res.records);

    const timeoutPromise = new Promise<null>((resolve) => {
      timerId = setTimeout(() => resolve(null), semanticTimeout);
    });

    const fastResult = await Promise.race([hybridPromise, timeoutPromise]);
    if (timerId !== null) {
      clearTimeout(timerId);
      timerId = null;
    }

    if (fastResult !== null) {
      return fastResult;
    }
  } catch {
    // If hybrid throws early, proceed to lexical fallback immediately
  } finally {
    if (timerId !== null) {
      clearTimeout(timerId);
      timerId = null;
    }
  }

  // If hybrid timed out or failed, attempt bounded lexical fallback
  const elapsed = Date.now() - startTime;
  const remainingMs = Math.max(50, timeoutMs - elapsed);

  let fallbackTimer: ReturnType<typeof setTimeout> | null = null;
  try {
    const lexPromise = executeHybridScan(env, { topic, limit: 5, embed: false })
      .then((res) => res.records);

    const fallbackTimeoutPromise = new Promise<FusedCandidate[]>((resolve) => {
      fallbackTimer = setTimeout(() => resolve([]), remainingMs);
    });

    return await Promise.race([lexPromise, fallbackTimeoutPromise]);
  } catch {
    return [];
  } finally {
    if (fallbackTimer !== null) {
      clearTimeout(fallbackTimer);
    }
  }
}

export interface BeforeAgentStartEvent {
  messageType: "user" | "custom";
  prompt?: string;
}

export interface BeforeAgentStartResult {
  message?: {
    customType: string;
    content: string;
    display: boolean;
    details?: unknown;
  };
}

export async function handleBeforeAgentStart(
  event: BeforeAgentStartEvent,
  ctx: PiCtx,
  envSource: EnvSource,
  options?: { budget?: number; timeoutMs?: number },
): Promise<BeforeAgentStartResult | null> {
  if (event.messageType !== "user" || typeof event.prompt !== "string") {
    return null;
  }

  const prompt = event.prompt;
  if (!shouldTriggerAutoRecall(prompt)) {
    return null;
  }

  try {
    const env = await envSource(ctx);
    if (!env.cfg.qmapBin) return null;

    const timeout = options?.timeoutMs ?? DEFAULT_AUTO_RECALL_TIMEOUT_MS;
    const candidates = await runScanWithTimeout(env, prompt, timeout);
    if (!candidates || candidates.length === 0) return null;

    const budget = options?.budget ?? DEFAULT_AUTO_RECALL_BUDGET;
    const contextContent = buildInjectedContext(candidates, budget);
    if (!contextContent) return null;

    return {
      message: {
        customType: "memory_context",
        content: contextContent,
        display: true,
        details: { refs: candidates.map((c) => c.ref) },
      },
    };
  } catch {
    // Non-blocking: fail open so agent turn is never disrupted
    return null;
  }
}
