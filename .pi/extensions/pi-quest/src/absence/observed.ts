// HIGH_LEVEL: #human absence — direct peer-tool questions still feed history.
// The agent may ask via a peer tool instead of quest_ask_human; those answers
// bypass timeout semantics but must still land in quest history for the
// validator. This observer only records — it never blocks or interferes.
import { getState, updateState } from "../app/store";
import { recordHumanAnswer } from "../domain/quest";
import type { Pi, ToolCallEvent, ToolResultEvent } from "../hooks/events";
import { onToolCall, onToolResult } from "../hooks/events";
import type { QuestConfig } from "../config";

const KNOWN_ASKING_TOOLS = ["ask_questions", "ask_user_question"];
const PENDING_CAP = 50;

let watched: string[] = [...KNOWN_ASKING_TOOLS];
const pending = new Map<string, string>();

export function refreshAskingTools(config: QuestConfig): void {
  watched = [...new Set([config.bindings.asking.tool, ...KNOWN_ASKING_TOOLS])];
}

function isAskingTool(toolName: string): boolean {
  return watched.includes(toolName);
}

function questionText(input: Record<string, unknown>): string | null {
  if (typeof input["question"] === "string" && input["question"].trim() !== "") {
    return (input["question"] as string).trim();
  }
  const list = input["questions"];
  if (!Array.isArray(list)) return null;
  const parts: string[] = [];
  for (const item of list) {
    if (typeof item !== "object" || item === null) continue;
    const record = item as Record<string, unknown>;
    const header = typeof record["header"] === "string" ? record["header"] as string : "";
    const question = typeof record["question"] === "string" ? record["question"] as string : "";
    const text = `${header} ${question}`.trim();
    if (text) parts.push(text);
  }
  return parts.length > 0 ? parts.join("; ") : null;
}

function answerText(event: ToolResultEvent): string | null {
  const details = event.details as Record<string, unknown> | undefined;
  if (details?.["cancelled"] === true) return null;
  const answers = details?.["answers"];
  if (Array.isArray(answers) && answers.length > 0) {
    const parts: string[] = [];
    for (const item of answers) {
      if (typeof item !== "object" || item === null) continue;
      const record = item as Record<string, unknown>;
      const answer = typeof record["answer"] === "string" ? record["answer"] as string : "";
      const preview = typeof record["preview"] === "string" ? record["preview"] as string : "";
      const text = `${answer} ${preview}`.trim();
      if (text) parts.push(text);
    }
    if (parts.length > 0) return parts.join("; ").slice(0, 2000);
  }
  const content = event.content.map((c) => c.text ?? "").join("\n").trim();
  return content ? content.slice(0, 2000) : null;
}

function remember(toolCallId: string, question: string): void {
  if (pending.size >= PENDING_CAP) {
    const oldest = pending.keys().next();
    if (!oldest.done) pending.delete(oldest.value);
  }
  pending.set(toolCallId, question);
}

export function installObservedQuestions(pi: Pi): void {
  onToolCall(pi, (event: ToolCallEvent) => {
    try {
      if (!isAskingTool(event.toolName)) return undefined;
      const question = questionText(event.input);
      if (question !== null) remember(event.toolCallId, question);
      return undefined;
    } catch {
      return undefined;
    }
  });
  onToolResult(pi, (event: ToolResultEvent) => {
    try {
      if (!isAskingTool(event.toolName)) return;
      const question = pending.get(event.toolCallId);
      if (question === undefined) return;
      pending.delete(event.toolCallId);
      const answer = answerText(event);
      if (answer === null || getState().qid === null) return;
      updateState((s) => recordHumanAnswer(s, question, answer, false));
    } catch {
      // Observation never breaks the agent.
    }
  });
}
