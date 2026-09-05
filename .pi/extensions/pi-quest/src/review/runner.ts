// HIGH_LEVEL: #interfaces — reviewers run isolated and read-only, abortable mid-run.
// HIGH_LEVEL: #independent review contexts — fresh context per run, no inherited reasoning.
// HIGH_LEVEL: #no direct mutation — the runner returns evidence, never transitions.
import type { Pi, PiCtx } from "../hooks/events";

export interface ReviewRunner {
  launch(prompt: string, signal: AbortSignal): Promise<LaunchResult>;
}

export interface LaunchResult {
  text: string;
}

const MAX_DURATION_MS = 300000;
const INACTIVITY_LIMIT_MS = 60000;

function uniqueId(prefix: string): string {
  return `${prefix}_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 8)}`;
}

export function isReviewerAvailable(pi: Pi, toolName = "subagent"): boolean {
  try {
    if (typeof pi.events?.on !== "function" || typeof pi.events?.emit !== "function") return false;
    return pi.getAllTools().some((t) => t.name === toolName);
  } catch {
    return false;
  }
}

export interface RunnerEnv {
  pi: Pi;
  ctx: PiCtx;
  ownerRunId: string;
  model?: string;
  toolName?: string;
}

export function createRunner(env: RunnerEnv): ReviewRunner | null {
  const toolName = env.toolName ?? "subagent";
  if (!isReviewerAvailable(env.pi, toolName)) return null;
  return {
    launch: (prompt, signal) => runReview(env, prompt, signal),
  };
}

function runReview(
  env: RunnerEnv,
  prompt: string,
  signal: AbortSignal,
): Promise<LaunchResult> {
  return new Promise((resolve, reject) => {
    if (signal.aborted) {
      reject(new Error(`review cancelled: ${String(signal.reason || "aborted")}`));
      return;
    }
    const requestId = uniqueId("quest_review");
    const nodeId = uniqueId("review");
    const startTime = Date.now();
    let lastActivityAt = startTime;
    let settled = false;
    const unsubs: Array<() => void> = [];

    const cleanup = () => {
      settled = true;
      clearTimeout(maxTimer);
      clearInterval(inactivityInterval);
      for (const unsub of unsubs) {
        try {
          unsub();
        } catch {
          // Unsubscribe is best-effort.
        }
      }
    };

    const fail = (err: Error) => {
      if (settled) return;
      cleanup();
      reject(err);
    };

    const maxTimer = setTimeout(() => {
      fail(new Error("Subagent execution timed out (quest_journal_deadline: max_duration)"));
    }, MAX_DURATION_MS);

    const inactivityInterval = setInterval(() => {
      const now = Date.now();
      if (now - lastActivityAt > INACTIVITY_LIMIT_MS && now - startTime > INACTIVITY_LIMIT_MS) {
        fail(new Error("Subagent execution timed out (quest_journal_deadline: inactivity)"));
      }
    }, 15000);

    signal.addEventListener("abort", () => {
      if (settled) return;
      try {
        env.pi.events.emit("prompt-template:subagent:cancel", {
          requestId,
          nodeId,
          ownerRunId: env.ownerRunId,
        });
      } catch {
        // Cancel is best-effort; the timers still bound the run.
      }
      fail(new Error(`review cancelled: ${String(signal.reason || "aborted")}`));
    }, { once: true });

    const noteActivity = () => {
      lastActivityAt = Date.now();
    };

    const onResponse = (data: unknown) => {
      const record = data as Record<string, unknown> | null;
      if (!record || record["requestId"] !== requestId || settled) return;
      const status = record["status"] as string | undefined;
      if (status && status !== "completed") {
        fail(new Error(`Subagent delegation failed (${status})`));
        return;
      }
      const result = record["result"] as Record<string, unknown> | undefined;
      let text = "";
      if (result?.["kind"] === "text" && typeof result["text"] === "string") {
        text = result["text"] as string;
      } else if (result?.["kind"] === "structured" && result["value"] !== undefined) {
        try {
          text = JSON.stringify(result["value"]);
        } catch {
          text = "";
        }
      } else {
        const fallback = record["contentText"] ?? record["text"];
        text = typeof fallback === "string" ? fallback : "";
      }
      cleanup();
      resolve({ text });
    };

    try {
      unsubs.push(env.pi.events.on("prompt-template:subagent:response", onResponse));
      unsubs.push(env.pi.events.on("prompt-template:subagent:update", noteActivity));
      unsubs.push(env.pi.events.on("subagent:activity", noteActivity));
      unsubs.push(env.pi.events.on("subagent:started", noteActivity));
      env.pi.events.emit("prompt-template:subagent:request", {
        requestId,
        ownerRunId: env.ownerRunId,
        nodeId,
        agent: "reviewer",
        task: prompt,
        context: "fresh",
        cwd: env.ctx.cwd,
        ...(env.model ? { model: env.model } : {}),
        result: { kind: "text" },
      });
    } catch (err) {
      fail(err instanceof Error ? err : new Error(String(err)));
    }
  });
}
