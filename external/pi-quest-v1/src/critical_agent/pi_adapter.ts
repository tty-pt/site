import {
  buildCriticalReviewPrompt,
  parseCriticalReviewResponse,
} from "./prompt.ts";
import { findProjectRoot } from "../diagnostic.ts";
import { logEvent, tryLog } from "../logging.ts";
import { getQuestId, getSessionId } from "../state.ts";
import {
  CriticalReviewer,
  ExtensionAPI,
  ExtensionContext,
  ReviewActivityStats,
  ReviewInput,
  ReviewResult,
  ReviewTimeoutLayer,
} from "../types.ts";

/**
 * Module-level map storing cancel identity (requestId, nodeId, ownerRunId)
 * keyed by reviewId (correlationId). Populated by the executor closure at
 * spawn time, consumed by PiSubagentReviewer.review() to wire the abort
 * signal to the host bridge cancel event.
 *
 * pi-subagents version: read from package.json at implementation time.
 */
export const cancelIdentityMap = new Map<
  string,
  { requestId: string; nodeId: string; ownerRunId: string }
>();

export type SubagentExecutorFn = (
  task: string,
  options?: {
    agent?: string;
    isCriticalReview?: boolean;
    reviewKind?: string;
    triggerReason?: string;
    model?: string;
    tools?: string[];
    async?: boolean;
    reviewId?: string;
    timeoutMs?: number;
    onActivity?: (activityEvent: any) => void;
  },
) => Promise<
  | string
  | {
    text?: string;
    content?: any;
    isError?: boolean;
    error?: any;
    childSessionId?: string;
    transcriptRef?: string;
  }
>;

export function classifyTimeoutLayer(errMessage: string): ReviewTimeoutLayer {
  const msg = (errMessage || "").toLowerCase();
  if (msg.includes("bridge") || msg.includes("event bridge")) {
    return "subagent_bridge_deadline";
  }
  if (
    msg.includes("process") ||
    msg.includes("killed") ||
    msg.includes("sigterm") ||
    msg.includes("sigkill") ||
    msg.includes("spawn")
  ) {
    return "child_process_deadline";
  }
  if (
    msg.includes("model") ||
    msg.includes("provider") ||
    msg.includes("rate limit") ||
    msg.includes("context") ||
    msg.includes("429") ||
    msg.includes("504") ||
    msg.includes("quota")
  ) {
    return "provider_model_timeout";
  }
  return "quest_journal_deadline";
}

export const BUILTIN_PROVIDERS = new Set([
  "openrouter",
  "openai",
  "anthropic",
  "google",
  "mistral",
  "bedrock",
  "ollama",
  "groq",
  "cerebras",
  "together",
  "deepseek",
  "xai",
  "github-copilot",
]);

/**
 * Normalizes a provider and model ID to a canonical reference resolvable by
 * isolated child subagent processes. In particular, extension providers registered
 * by ambient packages like `pi-free` (e.g. `kilo`, `cline`) are not loaded in
 * child sessions (`disableAmbientExtensions: true`), but their models point to
 * standard providers like `openrouter`. Mapping these to the built-in provider
 * ensures subagent launches succeed out of the box.
 */
export function normalizeReviewModel(
  rawProvider?: string,
  rawId?: string,
): string {
  if (!rawProvider && !rawId) return "";
  const provider = (rawProvider || "").toLowerCase().trim();
  const id = (rawId || "").trim();

  // Handle extension-registered proxy providers from pi-free (kilo, cline, etc.)
  if (provider === "kilo" || provider === "cline") {
    if (id.startsWith("openrouter/")) {
      return `openrouter/${id}`;
    }
    const slashIdx = id.indexOf("/");
    if (slashIdx > 0) {
      const subProvider = id.substring(0, slashIdx).toLowerCase();
      if (BUILTIN_PROVIDERS.has(subProvider)) {
        return id;
      }
    }
    return `openrouter/${id}`;
  }

  // If provider is already built-in, use standard provider/id format
  if (BUILTIN_PROVIDERS.has(provider)) {
    return id ? `${provider}/${id}` : provider;
  }

  // If provider is unknown/ambient extension, but id starts with a known provider:
  const slashIdx = id.indexOf("/");
  if (slashIdx > 0) {
    const inferred = id.substring(0, slashIdx).toLowerCase();
    if (BUILTIN_PROVIDERS.has(inferred)) {
      return id;
    }
  }

  return id ? `${provider}/${id}` : provider;
}

export function isModelResolutionOrProviderError(errMessage: string): boolean {
  const msg = (errMessage || "").toLowerCase();
  return (
    (msg.includes("model") &&
      (msg.includes("not found") || msg.includes("cannot find") ||
        msg.includes("unknown"))) ||
    msg.includes("unknown provider") ||
    msg.includes("provider_model_timeout") ||
    msg.includes("rate limit") ||
    msg.includes("429") ||
    msg.includes("504") ||
    msg.includes("quota")
  );
}

export function resolveDefaultReviewModel(ctx?: ExtensionContext): string {
  if (typeof process !== "undefined" && process.env) {
    const explicitReviewModel = process.env.PI_CRITICAL_REVIEW_MODEL ||
      process.env.PI_REVIEW_MODEL;
    if (explicitReviewModel) return String(explicitReviewModel);

    const provider = process.env.PI_PROVIDER;
    const model = process.env.PI_MODEL;
    if (provider && model) {
      return normalizeReviewModel(provider, model);
    }
    if (model) {
      return String(model);
    }
  }
  const ctxModel = (ctx as { model?: unknown })?.model;
  if (typeof ctxModel === "string") {
    const slashIdx = ctxModel.indexOf("/");
    if (slashIdx > 0) {
      return normalizeReviewModel(
        ctxModel.substring(0, slashIdx),
        ctxModel.substring(slashIdx + 1),
      );
    }
    return ctxModel;
  }
  if (ctxModel && typeof ctxModel === "object") {
    const m = ctxModel as { provider?: unknown; id?: unknown; name?: unknown };
    if (m.provider && m.id) {
      return normalizeReviewModel(String(m.provider), String(m.id));
    }
    if (m.id) return String(m.id);
    if (m.name) return String(m.name);
  }
  return "";
}

// In-memory or custom runner registry for dependency injection & testing
let customSubagentRunner: SubagentExecutorFn | null = null;

export function setCustomSubagentRunner(
  runner: SubagentExecutorFn | null,
): void {
  customSubagentRunner = runner;
}

export function getCustomSubagentRunner(): SubagentExecutorFn | null {
  return customSubagentRunner;
}

export function isSubagentToolRegistered(
  pi?: ExtensionAPI,
  _ctx?: ExtensionContext,
): boolean {
  if (customSubagentRunner) return true;
  if (typeof pi?.getAllTools === "function") {
    try {
      const tools = pi.getAllTools();
      if (Array.isArray(tools)) {
        return tools.some((t: any) => t?.name === "subagent");
      }
    } catch {}
  }
  return false;
}

/**
 * Resolve the working directory / search root for a launched subagent.
 *
 * #58: the Pi subagent's read/grep/search tools are rooted at the cwd passed on launch. On a stray
 * QUEST_REUSED mount the session cwd can point at the extension directory (.pi/extensions/pi-quest),
 * which makes the subagent search the extension's own source instead of the site repo and ENOENT-thrash.
 * Always anchor to the repository root (findProjectRoot strips trailing .pi/extensions/ paths).
 * A transparent re-anchor diagnostic is logged when the incoming cwd differs from the resolved root.
 */
export function resolveSubagentCwd(ctx?: ExtensionContext): string {
  const from = ctx?.cwd ||
    (typeof process !== "undefined" ? process.cwd() : "");
  const to = findProjectRoot(from);
  if (from && from !== to) {
    tryLog(
      "SUBAGENT_CWD_REANCHORED",
      `subagent cwd re-anchored to project root`,
      {
        quest: getQuestId(ctx) || "",
        from,
        to,
        reason: "wrong_search_root",
      },
      ctx,
    );
  }
  return to;
}

export function resolveSubagentExecutor(
  pi?: ExtensionAPI,
  ctx?: ExtensionContext,
): SubagentExecutorFn | null {
  if (customSubagentRunner) return customSubagentRunner;

  // Subagent extension supported bridge mechanism (pi.events bridge registered by pi-subagents)
  if (
    pi?.events &&
    typeof pi.events.on === "function" &&
    typeof pi.events.emit === "function"
  ) {
    if (!isSubagentToolRegistered(pi, ctx)) {
      return null;
    }
    return async (task: string, options?: any) => {
      return new Promise((resolve, reject) => {
        // Fresh, per-invocation-unique identity for the structured delegation transport.
        // The pi-subagents bridge keys active nodes by [ownerRunId, nodeId] and settles by
        // [requestId, ownerRunId, nodeId]; two reviews in the same turn must not collide, so
        // neither requestId nor nodeId may be reused (options.reviewId == correlationId ==
        // currentTurnCorrelationId is per-turn, not per-invocation).
        const requestId = `quest_review_${Date.now().toString(36)}_${
          Math.random().toString(36).slice(2, 8)
        }`;
        const nodeId = `review_${Date.now().toString(36)}_${
          Math.random().toString(36).slice(2, 8)
        }`;
        // Record cancel identity for signal→cancel wiring in PiSubagentReviewer.review()
        if (options?.reviewId) {
          cancelIdentityMap.set(options.reviewId, {
            requestId,
            nodeId,
            ownerRunId: getQuestId(ctx) || getSessionId(ctx),
          });
        }
        let unsubs: Array<(() => void) | void> = [];

        const maxDuration = options?.timeoutMs || 300000; // 5 minutes default
        const inactivityLimit = 60000; // 60 seconds of zero activity
        let lastActivityAt = Date.now();
        let childSessionId: string | undefined = undefined;

        const checkInactivity = () => {
          const now = Date.now();
          if (
            now - lastActivityAt > inactivityLimit &&
            now - startTime > inactivityLimit
          ) {
            cleanup();
            const err: any = new Error(
              "Subagent execution timed out (quest_journal_deadline: inactivity)",
            );
            err.timeoutLayer = "quest_journal_deadline";
            reject(err);
            return;
          }
        };

        const startTime = Date.now();
        const inactivityInterval = setInterval(checkInactivity, 15000);

        const maxTimer = setTimeout(() => {
          cleanup();
          const err: any = new Error(
            "Subagent execution timed out (quest_journal_deadline: max_duration)",
          );
          err.timeoutLayer = "quest_journal_deadline";
          reject(err);
        }, maxDuration);

        const cleanup = () => {
          clearTimeout(maxTimer);
          clearInterval(inactivityInterval);
          for (const u of unsubs) {
            if (typeof u === "function") u();
          }
          unsubs = [];
          // Clear cancel identity from map on settle
          if (options?.reviewId) cancelIdentityMap.delete(options.reviewId);
        };

        const handleActivity = (data: any) => {
          if (
            data &&
            (data.requestId === requestId ||
              data.runId === requestId ||
              data.id === requestId ||
              !data.requestId)
          ) {
            lastActivityAt = Date.now();
            if (data.childSessionId) childSessionId = data.childSessionId;
            if (typeof options?.onActivity === "function") {
              options.onActivity({ ...data, childSessionId });
            }
          }
        };

        const handleResponse = (data: any) => {
          if (data && data.requestId === requestId) {
            cleanup();
            const status = data.status as string | undefined;
            // Structured delegation response: errors are signaled by status !== "completed"
            // (no isError/contentText on the structured wire). A "completed" text result is
            // carried in result.text.
            if (status && status !== "completed") {
              const errorText = data.error ||
                (status === "invalid_request"
                  ? "Subagent delegation request was invalid"
                  : `Subagent delegation failed (${status})`);
              const err: any = new Error(errorText);
              err.timeoutLayer = classifyTimeoutLayer(errorText);
              reject(err);
            } else {
              let text = "";
              if (
                data.result?.kind === "text" &&
                typeof data.result.text === "string"
              ) {
                text = data.result.text;
              } else if (
                data.result?.kind === "structured" &&
                data.result.value !== undefined
              ) {
                try {
                  text = JSON.stringify(data.result.value);
                } catch {
                  text = "";
                }
              } else {
                text = data.contentText || data.text || "";
              }
              const transcriptRef = data.transcriptRef ||
                data.sessionPath ||
                (data.runId ? `.pi/sessions/${data.runId}.jsonl` : undefined) ||
                (childSessionId
                  ? `.pi/sessions/${childSessionId}.jsonl`
                  : undefined);
              resolve({
                text,
                childSessionId: data.childSessionId || childSessionId,
                transcriptRef,
              });
            }
          }
        };

        if (typeof pi.events!.on === "function") {
          unsubs.push(
            pi.events!.on("prompt-template:subagent:response", handleResponse),
          );

          // Structured delegation progress heartbeats (prompt-template:subagent:update).
          unsubs.push(
            pi.events!.on("prompt-template:subagent:update", handleActivity),
          );

          // Legacy host activity events retained for activity/heartbeat observability.
          unsubs.push(pi.events!.on("subagent:activity", handleActivity));
          unsubs.push(pi.events!.on("subagent:started", handleActivity));
          unsubs.push(pi.events!.on("subagent:tool_call", handleActivity));
          unsubs.push(pi.events!.on("subagent:tool_result", handleActivity));
          unsubs.push(pi.events!.on("subagent:turn_start", handleActivity));
          unsubs.push(pi.events!.on("subagent:turn_end", handleActivity));
        }

        const targetModel = options?.model !== undefined &&
            typeof options?.model === "string" &&
            options.model !== ""
          ? options.model
          : resolveDefaultReviewModel(ctx);

        // Emit a structured delegation request (pi-subagents bridge). Without ownerRunId/nodeId/
        // result the bridge routes to the legacy path, which rejects "Legacy prompt-template
        // direct delegation was removed". async is intentionally omitted: it is not a supported
        // delegation field and would cause an "Unsupported delegation field" parse rejection.
        pi.events!.emit("prompt-template:subagent:request", {
          requestId,
          ownerRunId: getQuestId(ctx) || getSessionId(ctx),
          nodeId,
          agent: options?.agent || "reviewer",
          task,
          context: "fresh",
          cwd: resolveSubagentCwd(ctx),
          ...(targetModel ? { model: targetModel } : {}),
          ...(maxDuration !== 300000 ? { timeoutMs: maxDuration } : {}),
          result: { kind: "text" },
        });
      });
    };
  }

  return null;
}

export function isSubagentAvailable(
  pi?: ExtensionAPI,
  ctx?: ExtensionContext,
): boolean {
  if (customSubagentRunner) return true;
  const registered = isSubagentToolRegistered(pi, ctx);
  if (!registered) return false;
  const executor = resolveSubagentExecutor(pi, ctx);
  return executor !== null;
}

/**
 * PiSubagentReviewer: The Pi-specific reviewer adapter.
 * Handles subagent discovery, invocation, timeouts, and read-only tool restrictions.
 * Implements the domain CriticalReviewer interface.
 */
export class PiSubagentReviewer implements CriticalReviewer {
  constructor(
    private pi?: ExtensionAPI,
    private ctx?: ExtensionContext,
    private explicitRunner?: SubagentExecutorFn | null,
  ) {}

  isAvailable(): boolean {
    if (this.explicitRunner || customSubagentRunner) return true;
    return (
      isSubagentToolRegistered(this.pi, this.ctx) &&
      resolveSubagentExecutor(this.pi, this.ctx) !== null
    );
  }

  async review(input: ReviewInput): Promise<ReviewResult> {
    if (input.signal?.aborted) {
      const err: any = new Error(
        `review cancelled: ${
          String((input.signal as { reason?: unknown }).reason || "aborted")
        }`,
      );
      err.name = "AbortError";
      throw err;
    }
    const executor = this.explicitRunner ||
      customSubagentRunner ||
      resolveSubagentExecutor(this.pi, this.ctx);
    if (!executor) {
      throw new Error("subagent_tool_not_executable");
    }
    const prompt = buildCriticalReviewPrompt(
      input.kind,
      input.questSlug,
      input.context,
      input.rebuttal,
      input.triggerReason,
      input.boundaryKey,
    );
    const targetModel = input.model && typeof input.model === "string"
      ? input.model
      : resolveDefaultReviewModel(this.ctx);

    const startTime = Date.now();
    let rawRes: any;

    // Wire cancel signal BEFORE executor call — listener looks up Map at emit
    // time (not capture time) so it always uses the latest identity, handling
    // the fallback retry case correctly.
    if (input.reviewId && input.signal) {
      input.signal.addEventListener("abort", () => {
        const id = cancelIdentityMap.get(input.reviewId!);
        if (id) {
          try {
            this.pi?.events?.emit("prompt-template:subagent:cancel", id);
          } catch {}
        }
      }, { once: true });
    }

    // Build ordered list of candidate models to try for review execution
    const candidateModels: Array<{ model?: string; label: string }> = [];
    candidateModels.push({
      model: targetModel,
      label: targetModel || "default",
    });

    // If targetModel starts with an extension provider prefix (e.g. kilo/ or cline/), add normalized openrouter candidate
    if (
      targetModel &&
      (targetModel.startsWith("kilo/") || targetModel.startsWith("cline/"))
    ) {
      const rest = targetModel.replace(/^(kilo|cline)\//, "");
      const norm = rest.startsWith("openrouter/")
        ? `openrouter/${rest}`
        : `openrouter/${rest}`;
      if (
        norm !== targetModel && !candidateModels.some((c) => c.model === norm)
      ) {
        candidateModels.push({ model: norm, label: norm });
      }
    }

    // Host default / unconstrained model (model: undefined activates Pi findInitialModel in child session)
    if (targetModel) {
      candidateModels.push({
        model: undefined,
        label: "host_default_unconstrained",
      });
    }

    // Explicit fallback env if configured
    if (
      typeof process !== "undefined" &&
      process.env?.PI_CRITICAL_REVIEW_MODEL_FALLBACK
    ) {
      const envFallback = String(process.env.PI_CRITICAL_REVIEW_MODEL_FALLBACK);
      if (!candidateModels.some((c) => c.model === envFallback)) {
        candidateModels.push({ model: envFallback, label: envFallback });
      }
    }

    const executeWithAgent = async (agent: string, modelToUse?: string) => {
      return await executor(prompt, {
        agent,
        isCriticalReview: true,
        reviewKind: input.kind,
        triggerReason: input.triggerReason || input.kind,
        ...(modelToUse !== undefined && modelToUse !== ""
          ? { model: modelToUse }
          : {}),
        tools: ["read", "grep", "find", "ls"],
        async: true,
        reviewId: input.reviewId,
        timeoutMs: input.timeoutMs,
        onActivity: input.onActivity,
      });
    };

    let executionSuccess = false;
    for (let i = 0; i < candidateModels.length; i++) {
      const candidate = candidateModels[i];
      try {
        try {
          rawRes = await executeWithAgent("reviewer", candidate.model);
        } catch (execErr: any) {
          const msg = String(execErr?.message || "");
          const isMutationAllowlistError =
            msg.toLowerCase().includes("mutation-capable") ||
            (msg.toLowerCase().includes("implementation task") &&
              msg.toLowerCase().includes("tool allowlist"));
          if (isMutationAllowlistError) {
            tryLog(
              "CRITICAL_REVIEW_FALLBACK",
              `reviewer agent rejected as implementation task; retrying with explore`,
              {
                quest: input.questSlug || "",
                reviewKind: input.kind,
                originalAgent: "reviewer",
                fallbackAgent: "explore",
                reason: "mutation_allowlist_mismatch",
                model: candidate.label,
              },
              this.ctx,
            );
            rawRes = await executeWithAgent("explore", candidate.model);
          } else {
            throw execErr;
          }
        }
        if (i > 0) {
          tryLog(
            "CRITICAL_REVIEW_MODEL_FALLBACK",
            `reviewer succeeded after model fallback from '${
              candidateModels[0].label
            }' to '${candidate.label}'`,
            {
              quest: input.questSlug || "",
              reviewKind: input.kind,
              fromModel: candidateModels[0].label,
              toModel: candidate.label,
              attempt: i + 1,
            },
            this.ctx,
          );
        }
        executionSuccess = true;
        break;
      } catch (err: any) {
        const msg = String(err?.message || "");
        const isModelErr = isModelResolutionOrProviderError(msg) ||
          classifyTimeoutLayer(msg) === "provider_model_timeout";

        if (isModelErr && i + 1 < candidateModels.length) {
          const nextCandidate = candidateModels[i + 1];
          tryLog(
            "CRITICAL_REVIEW_MODEL_FALLBACK",
            `reviewer model '${candidate.label}' failed (${msg}); retrying with fallback '${nextCandidate.label}'`,
            {
              quest: input.questSlug || "",
              reviewKind: input.kind,
              failedModel: candidate.label,
              fallbackModel: nextCandidate.label,
              error: msg,
              nextAttempt: i + 2,
            },
            this.ctx,
          );
          continue;
        }

        if (isModelErr) {
          tryLog(
            "CRITICAL_REVIEW_MODEL_EXHAUSTED",
            `all reviewer model candidates exhausted after '${candidate.label}' failure (${msg})`,
            {
              quest: input.questSlug || "",
              reviewKind: input.kind,
              failedModel: candidate.label,
              attempts: i + 1,
              error: msg,
            },
            this.ctx,
          );
        }

        const timeoutLayer = err?.timeoutLayer || classifyTimeoutLayer(msg);
        err.timeoutLayer = timeoutLayer;
        throw err;
      }
    }

    const durationMs = Date.now() - startTime;
    const rawResponseText = typeof rawRes === "string"
      ? rawRes
      : rawRes?.text ||
        (Array.isArray(rawRes?.content)
          ? rawRes.content.map((c: any) => c.text || "").join("\n")
          : "");

    const childSessionId = typeof rawRes === "object"
      ? rawRes?.childSessionId
      : undefined;
    const childTranscriptRef = typeof rawRes === "object"
      ? rawRes?.transcriptRef || rawRes?.childTranscriptRef
      : undefined;

    const parsed = parseCriticalReviewResponse(rawResponseText);
    return {
      ...parsed,
      rawText: rawResponseText,
      childSessionId,
      childTranscriptRef,
      durationMs,
    };
  }
}

// Default export alias for backward compatibility
export { PiSubagentReviewer as DefaultCriticalReviewer };
