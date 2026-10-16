import type { MmConfig } from "../config.ts";
import { judgeConfigured } from "../config.ts";
import { classifyConversationalTurn, cleanSummarySentence, isMemoryInquiry, isExplicitRememberDirective, isTrivialFarewell } from "../hooks/events.ts";
import { classifyQueryDomain } from "../tools/ranking.ts";
import type { JudgeAction, JudgeDecision, JudgeResult } from "./types.ts";

export function normalizeJevUrl(baseUrlOrUrl: string): string {
  const trimmed = baseUrlOrUrl.trim();
  if (trimmed.includes("/systemone")) {
    return trimmed.endsWith("/") ? trimmed : `${trimmed}/`;
  }
  return `${trimmed.replace(/\/+$/, "")}/api/v1/systemone/`;
}

export function parseJevAction(data: unknown): JudgeAction {
  if (typeof data !== "object" || data === null) {
    throw new Error("Jev response must be an object");
  }
  const rec = data as Record<string, unknown>;
  let answerVal: unknown;

  // 1. Primary documented path: answers.memory_action.choice
  if (typeof rec.answers === "object" && rec.answers !== null && !Array.isArray(rec.answers)) {
    const ansObj = rec.answers as Record<string, unknown>;
    const memAction = ansObj["memory_action"] ?? ansObj["action"] ?? Object.values(ansObj)[0];
    if (typeof memAction === "object" && memAction !== null) {
      const objVal = memAction as Record<string, unknown>;
      answerVal = objVal["choice"] ?? objVal["answer"] ?? objVal["value"];
    } else {
      answerVal = memAction;
    }
  }

  // 2. Defensive fallback: array answers
  if (answerVal === undefined && Array.isArray(rec.answers)) {
    const list = rec.answers;
    if (list.length === 0) {
      throw new Error("Jev response contains empty answers list");
    }
    const matched = list.find(
      (a: unknown) =>
        typeof a === "object" && a !== null &&
        ((a as Record<string, unknown>).id === "memory_action" || (a as Record<string, unknown>).question_id === "memory_action"),
    );
    if (matched && typeof matched === "object") {
      const matchRec = matched as Record<string, unknown>;
      answerVal = matchRec.choice ?? matchRec.answer ?? matchRec.value;
    } else if (typeof list[0] === "string") {
      answerVal = list[0];
    } else if (typeof list[0] === "object" && list[0] !== null) {
      const firstRec = list[0] as Record<string, unknown>;
      answerVal = firstRec.choice ?? firstRec.answer ?? firstRec.value;
    }
  }

  if (typeof answerVal !== "string") {
    throw new Error(`Jev response missing choice answer: ${JSON.stringify(data).slice(0, 100)}`);
  }

  const normalized = answerVal.trim().toLowerCase();
  if (normalized === "remember" || normalized.startsWith("remember")) {
    return "remember";
  }
  if (normalized === "update" || normalized.startsWith("update")) {
    return "update";
  }
  if (normalized === "skip" || normalized.startsWith("skip")) {
    return "skip";
  }
  throw new Error(`Invalid Jev choice answer: '${answerVal}' (expected 'remember', 'update', or 'skip')`);
}

export function extractCompactMemorySummary(
  prompt: string,
  response: string,
  threadContext?: string,
): { summary: string; type: "note" | "decision" | "episode" } {
  // 1. Check if classifyConversationalTurn has identified meaningful knowledge
  const classif = classifyConversationalTurn(prompt, response, threadContext);
  if (classif.meaningful && classif.summary && classif.summary.trim().length > 0) {
    return {
      summary: classif.summary.trim(),
      type: classif.type ?? "note",
    };
  }

  const p = prompt.trim();
  const r = response.trim();

  // 2. Personal provenance rule:
  // If the user prompt is a question or inquiry, or in the personal domain,
  // the assistant's reply must NEVER be recorded as evidence of a personal event!
  const isQuestion = p.endsWith("?") || isMemoryInquiry(p);
  const domain = classifyQueryDomain(p);
  if (isQuestion && (domain === "personal" || isMemoryInquiry(p))) {
    return {
      summary: "",
      type: "note",
    };
  }

  // Personal domain facts must come strictly from user statement, never assistant speculation
  if (domain === "personal") {
    if (p.length < 10) return { summary: "", type: "note" };
    const cleanedPersonal = cleanSummarySentence(p);
    return {
      summary: cleanedPersonal.length > 0 ? cleanedPersonal : p,
      type: "note",
    };
  }

  // 3. For technical exchanges where LLM judge decided remember, allow assistant's technical response
  const candidate = (p.length >= 15 && !p.endsWith("?")) ? p : (r.length >= 15 ? r : (p.length > 0 ? p : r));
  const cleaned = cleanSummarySentence(candidate);
  const finalSummary = cleaned.length > 0 ? cleaned : (p.length > 0 ? p : (r.length > 0 ? r : "Conversational memory"));

  return {
    summary: finalSummary,
    type: "note",
  };
}

export async function callJevJudge(
  prompt: string,
  response: string,
  cfg: MmConfig,
  fetchFn: typeof fetch = globalThis.fetch,
  threadContext?: string,
): Promise<JudgeDecision> {
  const rawUrl = cfg.judgeUrl;
  if (!rawUrl) throw new Error("No judgeUrl configured");

  const url = normalizeJevUrl(rawUrl);
  const timeoutMs = cfg.judgeTimeoutMs ?? 5000;
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);

  const headers: Record<string, string> = {
    "Content-Type": "application/json",
  };
  if (cfg.judgeKey) {
    headers["Authorization"] = `Bearer ${cfg.judgeKey}`;
  }

  const stateText = threadContext
    ? `${threadContext}\nCurrent User: ${prompt}\nCurrent Assistant: ${response}`
    : `User: ${prompt}\nAssistant: ${response}`;

  const reqBody: Record<string, unknown> = {
    model: cfg.judgeModel || "default",
    state: stateText,
    questions: {
      memory_action: {
        type: "choice",
        instructions: `Decide whether the user provided durable, useful new knowledge or instructions to remember, update an existing memory, or skip.
Remember criteria: User provided a new personal experience, factual statement, preference, technical discovery/decision, or explicit request to remember a fact.
Update criteria: User provided a refinement or correction to previously stored knowledge.
Skip criteria: User asked a question about existing knowledge, or the exchange is casual greeting, ordinary request, or assistant speculation/repetition without new user facts.`,
        criteria: {
          remember: "User provided a new personal experience, factual statement, technical discovery/decision, or explicit request to remember a fact",
          update: "User provided a refinement or correction to previously stored knowledge",
          skip: "User asked a question about existing knowledge, or the exchange is casual greeting, ordinary request, or assistant speculation/repetition without new user facts",
        },
      },
    },
  };

  try {
    const res = await fetchFn(url, {
      method: "POST",
      headers,
      body: JSON.stringify(reqBody),
      signal: controller.signal,
    });

    if (!res.ok) {
      const errText = await res.text().catch(() => "");
      throw new Error(`Jev service HTTP error ${res.status}: ${errText.slice(0, 100)}`);
    }

    const data = await res.json();
    const action = parseJevAction(data);

    if (action === "skip") {
      return {
        action: "skip",
        reason: "Classified as skip by Jev System One",
      };
    }

    const compact = extractCompactMemorySummary(prompt, response, threadContext);
    if (!compact.summary || compact.summary.trim().length === 0) {
      return {
        action: "skip",
        reason: "Inquiry or conversational exchange without verifiable new user facts",
      };
    }

    if (action === "update") {
      return {
        action: "update",
        summary: compact.summary,
        reason: "Classified as update by Jev System One",
      };
    }

    return {
      action: "remember",
      summary: compact.summary,
      type: compact.type,
      reason: "Classified as remember by Jev System One",
    };
  } finally {
    clearTimeout(timer);
  }
}

export function callLlmJudge(
  prompt: string,
  response: string,
  cfg: MmConfig,
  fetchFn: typeof fetch = globalThis.fetch,
  threadContext?: string,
): Promise<JudgeDecision> {
  return callJevJudge(prompt, response, cfg, fetchFn, threadContext);
}

export async function evaluateTurnWithJudge(
  prompt: string,
  response: string,
  cfg: MmConfig,
  fetchFn: typeof fetch = globalThis.fetch,
  threadContext?: string,
): Promise<JudgeResult> {
  // 1. Judge mode off: disable automatic conversational capture
  if (cfg.judgeMode === "off") {
    return {
      decision: { action: "skip", reason: "Automatic conversational capture is disabled (judgeMode=off)" },
      source: "disabled",
    };
  }

  // 2. Explicit forget directive
  const p = prompt.trim();
  if (/^(?:forget\s+(?:this|that|everything|our\s+previous)|do\s+not\s+remember)\b/i.test(p)) {
    return {
      decision: { action: "skip", reason: "Explicit forget directive in user prompt" },
      source: "explicit",
    };
  }

  // 2.5. Trivial farewells: always skip
  if (isTrivialFarewell(p)) {
    return {
      decision: { action: "skip", reason: "Trivial farewell greeting" },
      source: "explicit",
    };
  }

  // 3. Questions asking what the system already remembers: always skip (query, not new fact)
  if (isMemoryInquiry(p)) {
    return {
      decision: { action: "skip", reason: "Query about existing memory is not new knowledge" },
      source: "explicit",
    };
  }

  // 4. Explicit remember directives: strictly take precedence over automatic LLM or heuristic judgement
  const rem = isExplicitRememberDirective(p);
  if (rem.isDirective && rem.fact) {
    const summary = cleanSummarySentence(rem.fact);
    return {
      decision: {
        action: "remember",
        summary,
        type: "note",
        findings: [summary],
      },
      source: "explicit",
    };
  }

  // 3. Heuristic mode or judge not configured: use deterministic classification
  if (cfg.judgeMode === "heuristic" || !judgeConfigured(cfg)) {
    const classif = classifyConversationalTurn(prompt, response, threadContext);
    if (classif.meaningful && classif.summary) {
      return {
        decision: {
          action: "remember",
          summary: classif.summary,
          type: classif.type ?? "note",
          findings: classif.findings,
        },
        source: "heuristic",
      };
    }
    return {
      decision: { action: "skip", reason: "Deterministic heuristic found no memorable knowledge" },
      source: "heuristic",
    };
  }

  // 4. Auto mode with configured judge endpoint
  try {
    const decision = await callLlmJudge(prompt, response, cfg, fetchFn, threadContext);
    return {
      decision,
      source: "llm",
    };
  } catch (err: unknown) {
    const errMsg = err instanceof Error ? err.message : String(err);
    console.warn(`[pi-mm] Judge service unavailable (${errMsg}). Applying fallback '${cfg.judgeFallback ?? "heuristic"}'.`);

    if (cfg.judgeFallback === "skip") {
      return {
        decision: { action: "skip", reason: `Judge failed (${errMsg}), fallback is skip` },
        source: "heuristic",
        degraded: true,
        error: errMsg,
      };
    }

    // Default fallback: deterministic heuristic
    const classif = classifyConversationalTurn(prompt, response, threadContext);
    if (classif.meaningful && classif.summary) {
      return {
        decision: {
          action: "remember",
          summary: classif.summary,
          type: classif.type ?? "note",
          findings: classif.findings,
        },
        source: "heuristic",
        degraded: true,
        error: errMsg,
      };
    }

    return {
      decision: { action: "skip", reason: `Judge failed (${errMsg}), heuristic found no memorable knowledge` },
      source: "heuristic",
      degraded: true,
      error: errMsg,
    };
  }
}
