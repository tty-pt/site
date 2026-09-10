// HIGH_LEVEL: #human absence — question providers; built-in input fallback.
// SPEC: B1.9 (ask-with-default + timeout race).
// A provider renders a question through a working invocation protocol and
// owns the full lifecycle (prompt, timeout, recording, notify). Bridge
// providers plug in here when a tool offers a subagent-style
// request/response channel; the built-in input provider is always
// available last, so dispatch never fails.
import type { Pi, PiCtx } from "../hooks/events";
import { askWithDefault, type AskResult } from "./ask";

export interface QuestionRequest {
  question: string;
  defaultAnswer: string;
  timeoutMs?: number;
}

export interface QuestionProvider {
  name: string;
  available(pi: Pi): boolean;
  ask(pi: Pi, ctx: PiCtx, req: QuestionRequest, signal?: AbortSignal): Promise<AskResult>;
}

const inputProvider: QuestionProvider = {
  name: "input",
  available: () => true,
  ask: (pi, ctx, req) =>
    askWithDefault(pi, ctx, {
      question: req.question,
      defaultAnswer: req.defaultAnswer,
      timeoutMs: req.timeoutMs,
    }),
};

export function selectProvider(pi: Pi, extra: QuestionProvider[] = []): QuestionProvider {
  for (const provider of extra) {
    try {
      if (provider.available(pi)) return provider;
    } catch {
      // A faulting provider never blocks the fallback chain.
    }
  }
  return inputProvider;
}
