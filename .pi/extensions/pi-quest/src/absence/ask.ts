// HIGH_LEVEL: #human absence — ask-with-default + timeout race.
// HIGH_LEVEL: #interfaces — prompting uses pi UI; providers dispatch it.
// SPEC: B1.9.
import { getState, updateState } from "../app/store";
import { emitNow, sendSteer } from "../app/interpreter";
import { readQuestConfig } from "../config";
import { recordHumanAnswer } from "../domain/quest";
import { GO_PATTERN } from "../drafting/reviews";
import type { Pi, PiCtx } from "../hooks/events";

export interface PendingQuestion {
  question: string;
  at: number;
  resolve: ((answer: string | null) => void) | null;
}

const pendingByQid = new Map<string, PendingQuestion>();
const LATE_WINDOW_MS = 15 * 60 * 1000;

export interface AskArgs {
  question: string;
  defaultAnswer: string;
  timeoutMs?: number;
}

export interface AskResult {
  answer: string;
  source: "user" | "default";
}

export async function askWithDefault(pi: Pi, ctx: PiCtx, args: AskArgs): Promise<AskResult> {
  const state = getState();
  const qid = state.qid ?? "none";
  const config = await readQuestConfig(ctx.cwd);
  const timeoutMs = args.timeoutMs ?? config.askTimeoutMs;
  const finish = (answer: string, source: AskResult["source"], late: boolean): AskResult => {
    updateState((s) => recordHumanAnswer(s, args.question, answer, late));
    emitNow(pi);
    return { answer, source };
  };
  if (!ctx.hasUI || timeoutMs === 0) {
    pendingByQid.set(qid, { question: args.question, at: Date.now(), resolve: null });
    if (ctx.hasUI) {
      sendSteer(pi, `Question not shown (zero wait): defaulting to "${args.defaultAnswer}". No human checkpoint occurred.`);
      ctx.ui.notify(`Question (zero wait): ${args.question.slice(0, 120)} — defaulting to "${args.defaultAnswer}"`, "warning");
    } else {
      sendSteer(pi, `No interactive UI present — this question was not shown to a human. Answering with default "${args.defaultAnswer}". A quest_ask_human default is not live human approval.`);
      ctx.ui.notify(`Question (no UI): ${args.question.slice(0, 120)} — defaulting to "${args.defaultAnswer}"`, "warning");
    }
    return finish(args.defaultAnswer, "default", false);
  }
  const answer = await new Promise<string | null>((resolve) => {
    pendingByQid.set(qid, { question: args.question, at: Date.now(), resolve });
    let done = false;
    const settle = (value: string | null) => {
      if (done) return;
      done = true;
      clearTimeout(timer);
      resolve(value);
    };
    const timer = timeoutMs < 0
      ? undefined
      : setTimeout(() => {
          ctx.ui.notify(`Question timed out: ${args.question.slice(0, 120)} — defaulting to "${args.defaultAnswer}"`, "warning");
          settle(null);
        }, timeoutMs);
    void ctx.ui.input(`Quest ${qid}: ${args.question}`, args.defaultAnswer, timeoutMs > 0 ? { timeout: timeoutMs } : undefined)
      .then((value) => settle(value ?? null), () => settle(null));
  });
  const pending = pendingByQid.get(qid);
  if (pending) pending.resolve = null;
  if (answer === null) return finish(args.defaultAnswer, "default", false);
  return finish(answer, "user", false);
}

export function noteLateAnswer(pi: Pi, text: string): boolean {
  const state = getState();
  if (state.qid === null) return false;
  const pending = pendingByQid.get(state.qid);
  if (pending === undefined) return false;
  if (Date.now() - pending.at > LATE_WINDOW_MS) {
    pendingByQid.delete(state.qid);
    return false;
  }
  const trimmed = text.trim();
  if (trimmed === "") return false;
  if (trimmed.startsWith("/")) return false;
  if (GO_PATTERN.test(trimmed)) return false;
  if (trimmed.length >= 500) return false;
  if (pending.resolve !== null) {
    const resolve = pending.resolve;
    pending.resolve = null;
    resolve(trimmed);
    return true;
  }
  updateState((s) => recordHumanAnswer(s, pending.question, trimmed, true));
  emitNow(pi);
  sendSteer(pi, `Late human answer applied as refinement: "${trimmed.slice(0, 300)}"`);
  return true;
}

export function pendingQuestion(qid: string): PendingQuestion | undefined {
  return pendingByQid.get(qid);
}
