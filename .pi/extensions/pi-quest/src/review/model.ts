// HIGH_LEVEL: #review and validation communication.
// HIGH_LEVEL: #review request — brief identifies qid, type, target, plan, evidence, criteria.
// HIGH_LEVEL: #independent review contexts — fresh context per run, no inherited reasoning.
// Pure reviewer launch inputs: a provider-agnostic candidate list plus an
// optional thinking level. This module holds no provider knowledge — the
// parent model passes through verbatim, or is omitted entirely (inherit) so
// the child host and its per-agent overrides govern. Child hosts may
// fabricate a literal `:level` thinking suffix (e.g. `some/model:high`) that
// a registry never registered — the flow therefore also retries the
// suffix-stripped variant instead of failing.
import type { PiCtx } from "../hooks/events";

// Thinking levels of the delegation contract. Validated here, never chosen:
// absent means inherit.
export const THINKING_LEVELS: ReadonlySet<string> = new Set([
  "off",
  "minimal",
  "low",
  "medium",
  "high",
  "xhigh",
  "max",
]);

function readEnv(name: string): string | undefined {
  try {
    const proc = (globalThis as Record<string, unknown>)["process"] as
      | { env?: Record<string, unknown> }
      | undefined;
    const value = proc?.env?.[name];
    return typeof value === "string" && value.length > 0 ? value : undefined;
  } catch {
    return undefined;
  }
}

// Strips a trailing `:level` thinking suffix from a model id. The id is never
// renamed or remapped — a suffixed id some registries cannot resolve may
// still run as the bare base id.
export function stripThinkingSuffix(model: string): string {
  const trimmed = model.trim();
  const colonIdx = trimmed.lastIndexOf(":");
  if (colonIdx <= 0) return trimmed;
  const suffix = trimmed.substring(colonIdx + 1).toLowerCase();
  return THINKING_LEVELS.has(suffix) ? trimmed.substring(0, colonIdx) : trimmed;
}

// Verbatim passthrough: explicit env first, then the launcher provider/model
// pair, then the live context model. Empty means inherit (the flow then omits
// the `model` field when delegating).
export function resolveDefaultReviewModel(ctx?: PiCtx): string {
  const explicit = readEnv("PI_CRITICAL_REVIEW_MODEL") ?? readEnv("PI_REVIEW_MODEL");
  if (explicit !== undefined) return explicit;
  const provider = readEnv("PI_PROVIDER");
  const model = readEnv("PI_MODEL");
  if (provider !== undefined && model !== undefined) return `${provider}/${model}`;
  if (model !== undefined) return model;
  const ctxModel = (ctx as { model?: unknown } | undefined)?.model;
  if (typeof ctxModel === "string") return ctxModel;
  if (ctxModel !== null && typeof ctxModel === "object") {
    const m = ctxModel as { provider?: unknown; id?: unknown; name?: unknown };
    if (typeof m.provider === "string" && typeof m.id === "string") return `${m.provider}/${m.id}`;
    if (typeof m.id === "string") return m.id;
    if (typeof m.name === "string") return m.name;
  }
  return "";
}

// Optional thinking level for the delegation request. Absent means inherit —
// the extension never fabricates a level the registry may not carry.
export function resolveReviewThinking(): string | undefined {
  const raw = readEnv("PI_REVIEW_THINKING") ?? readEnv("PI_CRITICAL_REVIEW_THINKING");
  if (raw === undefined) return undefined;
  const level = raw.toLowerCase().trim();
  return THINKING_LEVELS.has(level) ? level : undefined;
}

export type ReviewTimeoutLayer =
  | "subagent_bridge_deadline"
  | "child_process_deadline"
  | "provider_model_timeout"
  | "quest_journal_deadline";

export function classifyTimeoutLayer(errMessage: string): ReviewTimeoutLayer {
  const msg = (errMessage || "").toLowerCase();
  if (msg.includes("bridge") || msg.includes("event bridge")) return "subagent_bridge_deadline";
  if (msg.includes("process") || msg.includes("killed") || msg.includes("sigterm") ||
    msg.includes("sigkill") || msg.includes("spawn")) return "child_process_deadline";
  if (msg.includes("model") || msg.includes("provider") || msg.includes("rate limit") ||
    msg.includes("context") || msg.includes("429") || msg.includes("504") ||
    msg.includes("quota")) return "provider_model_timeout";
  return "quest_journal_deadline";
}

export function isModelResolutionOrProviderError(errMessage: string): boolean {
  const msg = (errMessage || "").toLowerCase();
  return (
    (msg.includes("model") &&
      (msg.includes("not found") || msg.includes("cannot find") || msg.includes("unknown"))) ||
    msg.includes("unknown provider") ||
    msg.includes("provider_model_timeout") ||
    msg.includes("rate limit") ||
    msg.includes("429") ||
    msg.includes("504") ||
    msg.includes("quota")
  );
}

export interface ReviewModelCandidate {
  model?: string;
  label: string;
}

// Ordered launch candidates: the resolved target first (it may carry a
// thinking suffix the child registry never registered), then the
// suffix-stripped variant, then the operator fallback. An empty target yields
// a single inherit attempt (no `model` field is sent), plus the fallback when
// one is configured.
export function reviewModelCandidates(targetModel: string): ReviewModelCandidate[] {
  const target = targetModel.trim();
  const candidates: ReviewModelCandidate[] = target === ""
    ? [{ label: "inherit" }]
    : [{ model: target, label: target }];
  if (target !== "") {
    const stripped = stripThinkingSuffix(target);
    if (stripped !== "" && stripped !== target) {
      candidates.push({ model: stripped, label: stripped });
    }
  }
  const envFallback = readEnv("PI_CRITICAL_REVIEW_MODEL_FALLBACK");
  if (envFallback !== undefined) {
    const fallback = envFallback.trim();
    if (fallback !== "" && !candidates.some((c) => c.model === fallback)) {
      candidates.push({ model: fallback, label: fallback });
    }
  }
  return candidates;
}
