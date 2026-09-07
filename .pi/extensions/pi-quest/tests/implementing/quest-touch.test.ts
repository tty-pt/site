import { check } from "../check.ts";
import { mkdirSync, mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import { createDraft, createQuest, IDLE_STATE, promote } from "../../src/domain/quest.ts";
import { draftPath } from "../../src/domain/paths.ts";
import type { Qid } from "../../src/domain/qid.ts";
import type { PiCtx, ToolResultEvent } from "../../src/hooks/events.ts";
import { hashContent } from "../../src/drafting/reviews.ts";
import { watchImplementingDocBoot } from "../../src/implementing/quest-touch.ts";
import { installValidation } from "../../src/validation/index.ts";
import { stopBlink } from "../../src/durability/status.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

const QID = "abc123" as Qid;

function docText(plan: string, complete: boolean): string {
  return [
    "## Requirements",
    "- first requirement",
    "",
    "## Implementation Plan",
    plan,
    "",
    "## Status",
    "- Phase: implementing",
    `- Complete: ${complete}`,
    "",
  ].join("\n");
}

function writeDoc(cwd: string, text: string): void {
  mkdirSync(join(cwd, ".pi", "quest", "future"), { recursive: true });
  Deno.writeTextFileSync(join(cwd, draftPath(QID)), text);
}

function busPi(
  cwd: string,
): {
  pi: ReturnType<typeof fakePi>;
  emitted: Array<{ event: string; data: unknown }>;
  toolResult: (event: Partial<ToolResultEvent>) => void;
  feed: (data: unknown) => void;
} {
  const pi = fakePi();
  pi.toolNames = ["subagent"];
  const emitted: Array<{ event: string; data: unknown }> = [];
  const bus = new Map<string, Array<(data: unknown) => void>>();
  (pi as { events: unknown }).events = {
    on: (event: string, handler: (data: unknown) => void) => {
      const list = bus.get(event) ?? [];
      list.push(handler);
      bus.set(event, list);
      return () => {};
    },
    emit: (event: string, data: unknown) => {
      emitted.push({ event, data });
    },
  };
  const onHandlers = new Map<string, Array<(event: ToolResultEvent, ctx: PiCtx) => unknown>>();
  (pi as unknown as { on: (event: string, handler: (event: ToolResultEvent, ctx: PiCtx) => unknown) => void }).on = (
    event: string,
    handler: (event: ToolResultEvent, ctx: PiCtx) => unknown,
  ) => {
    const list = onHandlers.get(event) ?? [];
    list.push(handler);
    onHandlers.set(event, list);
  };
  const toolResult = (partial: Partial<ToolResultEvent>) => {
    const event: ToolResultEvent = {
      type: "tool_result",
      toolCallId: "t1",
      toolName: "edit",
      input: {},
      content: [],
      isError: false,
      ...partial,
    };
    for (const handler of onHandlers.get("tool_result") ?? []) handler(event, fakeCtx(cwd));
  };
  const feed = (data: unknown) => {
    for (const handler of bus.get("prompt-template:subagent:response") ?? []) handler(data);
  };
  return { pi, emitted, toolResult, feed };
}

function implementing() {
  const s = createDraft(createQuest("req", QID), "thing");
  return { ...s, draft: { ...s.draft!, planAuthored: true } };
}

function wait(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function baselined(cwd: string, text: string, plan: string) {
  replaceState({
    ...implementing(),
    phase: "implementing" as const,
    draft: { ...implementing().draft!, contentHash: hashContent(text), lastReviewedPlan: plan },
  });
  void cwd;
}

Deno.test("implementing quest-doc Complete:true touch boots the validator in-turn", async () => {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-touch-"));
  writeDoc(cwd, docText("Do the work in order.", true));
  replaceState({ ...promote(implementing(), "review"), phase: "implementing" as const });
  const { pi, emitted, toolResult, feed } = busPi(cwd);
  watchImplementingDocBoot(pi);
  toolResult({ toolName: "edit", input: { path: draftPath(QID) } });
  await wait(40);
  const request = emitted.find((e) => e.event === "prompt-template:subagent:request");
  check(request !== undefined, "validator request launched in-turn");
  check(getState().phase === "validating", "phase moved to validating");
  check(pi.sent.some((s) => String(s.message.content).includes("marked complete")), "completion steer sent");
  feed({
    requestId: (request!.data as Record<string, unknown>)["requestId"],
    status: "completed",
    result: { kind: "text", text: "VERDICT: FAIL\nSEVERITY: HIGH\nFINDINGS: does not meet the plan\n" },
  });
  await wait(40);
  check(getState().phase === "implementing", "FAIL demotes back to implementing");
  const doc = readFileSync(join(cwd, draftPath(QID)), "utf8");
  check(doc.includes("Phase: implementing"), "doc status reset to implementing on FAIL demote");
  check(doc.includes("Complete: false"), "doc completion marker reset so a fresh claim re-arms");
  replaceState(IDLE_STATE);
  stopBlink();
});

Deno.test("status-only quest-doc touch rebaselines without booting a revision", async () => {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-touch-"));
  const text = docText("Do the work in order.", false);
  writeDoc(cwd, text);
  baselined(cwd, text, "Do the work in order.");
  const { pi, emitted, toolResult } = busPi(cwd);
  watchImplementingDocBoot(pi);
  toolResult({ toolName: "write", input: { path: draftPath(QID) } });
  await wait(40);
  check(emitted.length === 0, "no review request for a status-only touch");
  check(getState().phase === "implementing", "still implementing");
  check((getState().draft?.planRevisions ?? []).length === 0, "no revision recorded");
  replaceState(IDLE_STATE);
  void pi;
  stopBlink();
});

Deno.test("plan-section change in the quest doc boots a revision review in-turn", async () => {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-touch-"));
  writeDoc(cwd, docText("Do the work in order.", false));
  baselined(cwd, docText("Do the work in order.", false), "Do the work in order.");
  writeDoc(cwd, docText("Do the work compressively, then verify.", false));
  const { pi, emitted, toolResult, feed } = busPi(cwd);
  watchImplementingDocBoot(pi);
  toolResult({ toolName: "edit", input: { path: draftPath(QID) } });
  await wait(40);
  check((getState().draft?.planRevisions ?? []).length === 1, "plan revision recorded");
  const request = emitted.find((e) => e.event === "prompt-template:subagent:request");
  check(request !== undefined, "revision review booted in-turn");
  const revisedHash = hashContent(docText("Do the work compressively, then verify.", false));
  feed({
    requestId: (request!.data as Record<string, unknown>)["requestId"],
    status: "completed",
    result: { kind: "text", text: "VERDICT: PASS\nSEVERITY: NONE\n" },
  });
  await wait(40);
  check(getState().draft?.approvedPlanHash === revisedHash, "reviewed revision adopted as the approved binding");
  check(getState().phase === "implementing", "still implementing after adoption");
  replaceState(IDLE_STATE);
  void pi;
  stopBlink();
});

Deno.test("validating quest-doc touch boots a fresh validator against the new fingerprint", async () => {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-touch-"));
  writeDoc(cwd, docText("Do the work in order.", true));
  const validating = {
    ...promote(implementing(), "review"),
    phase: "validating" as const,
    lastReview: {
      verdict: "PASS" as const,
      target: "53a063121c2600a62874865a1d08e270005cc483aa59271d547d08b57bcfea34",
      findings: "stale pass from before the change",
    },
  };
  replaceState(validating);
  const { pi, emitted, toolResult, feed } = busPi(cwd);
  installValidation(pi);
  toolResult({ toolName: "write", input: { path: draftPath(QID) } });
  await wait(40);
  const request = emitted.find((e) => e.event === "prompt-template:subagent:request");
  check(request !== undefined, "validating touch boots a fresh validator in-turn");
  feed({
    requestId: (request!.data as Record<string, unknown>)["requestId"],
    status: "completed",
    result: { kind: "text", text: "VERDICT: FAIL\nSEVERITY: HIGH\nFINDINGS: changed work still unmet\n" },
  });
  await wait(40);
  check(getState().phase === "implementing", "changed-work FAIL demotes back to implementing");
  replaceState(IDLE_STATE);
  void pi;
  stopBlink();
});