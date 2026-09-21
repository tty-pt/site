import { check } from "../check.ts";
import { mkdirSync, mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import { DEFAULT_CONFIG } from "../../src/config.ts";
import { createQuest, IDLE_STATE, type QuestState } from "../../src/domain/quest.ts";
import { createDraft } from "../../src/domain/transitions.ts";
import { addChild } from "../../src/domain/children.ts";
import type { Qid } from "../../src/domain/qid.ts";
import { draftPath } from "../../src/domain/paths";
import { bootDraftReview } from "../../src/drafting/reviews.ts";
import { hashContent } from "../../src/drafting/plan-text.ts";
import { barePi, fakeCtx, fakePi } from "../fake-pi.ts";

const QID = "abc123" as Qid;

function draftFile(cwd: string, evidence: boolean): string {
  const content = [
    "## Requirements",
    "- first requirement",
    "- second requirement",
    "",
    ...(evidence ? ["## Evidence", "- found in the code", ""] : []),
    "## Implementation Plan",
    "Do the work in order.",
    "",
  ].join("\n");
  mkdirSync(join(cwd, ".pi", "quest", "future"), { recursive: true });
  writeFileSync(join(cwd, draftPath(QID)), content);
  return hashContent(content);
}

function busPi(): ReturnType<typeof fakePi> & { emitted: Array<{ event: string; data: unknown }>; feed: (data: unknown) => void } {
  const pi = fakePi();
  pi.toolNames = ["subagent"];
  const emitted: Array<{ event: string; data: unknown }> = [];
  const handlers = new Map<string, Array<(data: unknown) => void>>();
  (pi as { events: unknown }).events = {
    on: (event: string, handler: (data: unknown) => void) => {
      const list = handlers.get(event) ?? [];
      list.push(handler);
      handlers.set(event, list);
      return () => {};
    },
    emit: (event: string, data: unknown) => {
      emitted.push({ event, data });
    },
  };
  const feed = (data: unknown) => {
    for (const handler of handlers.get("prompt-template:subagent:response") ?? []) handler(data);
  };
  return Object.assign(pi, { emitted, feed });
}

async function runPassVerdict(evidence: boolean): Promise<{ phase: string; steered: string; woke: boolean }> {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-promote-"));
  const target = draftFile(cwd, evidence);
  const drafting = createDraft(createQuest("work", QID), "work");
  replaceState({ ...drafting, draft: { ...drafting.draft!, planAuthored: true } });
  const pi = busPi();
  const pending = bootDraftReview(pi, fakeCtx(cwd), target, DEFAULT_CONFIG);
  await new Promise((r) => setTimeout(r, 50));
  const request = pi.emitted.find((e) => e.event === "prompt-template:subagent:request");
  check(request !== undefined, "review launched");
  pi.feed({
    requestId: (request!.data as Record<string, unknown>)["requestId"],
    status: "completed",
    result: { kind: "text", text: "VERDICT: PASS\nSEVERITY: NONE\n" },
  });
  await pending;
  const steered = pi.sent.map((s) => String(s.message.content)).join("\n");
  const woke = pi.sent.some((s) => s.options?.triggerTurn === true);
  const phase = getState().phase;
  replaceState(IDLE_STATE);
  return { phase, steered, woke };
}

Deno.test("review PASS promotes with recorded research", async () => {
  const done = await runPassVerdict(true);
  check(done.phase === "implementing", "promoted");
  check(done.steered.includes("promoted to implementing"), "promotion announced");
  check(done.woke, "verdict wakes a new turn");
});

Deno.test("review PASS withholds promotion without recorded research", async () => {
  const done = await runPassVerdict(false);
  check(done.phase === "drafting", "stays drafting");
  check(done.steered.includes("needs recorded research"), "withhold explains");
  check(done.steered.includes('"go"'), "go escape offered");
});

Deno.test("review PASS attaches advisories to the promotion wake", async () => {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-promote-"));
  const target = draftFile(cwd, true);
  const drafting = createDraft(createQuest("work", QID), "work");
  replaceState({ ...drafting, draft: { ...drafting.draft!, planAuthored: true } });
  const pi = busPi();
  const pending = bootDraftReview(pi, fakeCtx(cwd), target, DEFAULT_CONFIG);
  try {
    await new Promise((r) => setTimeout(r, 50));
    const request = pi.emitted.find((e) => e.event === "prompt-template:subagent:request");
    check(request !== undefined, "review launched");
    pi.feed({
      requestId: (request!.data as Record<string, unknown>)["requestId"],
      status: "completed",
      result: {
        kind: "text",
        text: "VERDICT: PASS\nSEVERITY: NONE\nSUPPORTING FINDINGS:\n- Issue: solid\n  Evidence: checked\n\nADVISORIES:\n- confirm the choice\n",
      },
    });
    await pending;
    const steered = pi.sent.map((s) => String(s.message.content)).join("\n");
    check(getState().phase === "implementing", "still promotes");
    check(steered.includes("promoted to implementing"), "promotion announced");
    check(steered.includes("confirm the choice"), "advisory attached, not filed as findings");
  } finally {
    replaceState(IDLE_STATE);
  }
});

Deno.test("no reviewer keeps drafting and steers only a live user 'go'", async () => {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-promote-"));
  const target = draftFile(cwd, true);
  const drafting = createDraft(createQuest("work", QID), "work");
  replaceState({ ...drafting, draft: { ...drafting.draft!, planAuthored: true } });
  const pi = barePi(); // no reviewer transport at all => no-runner
  await bootDraftReview(pi, fakeCtx(cwd), target, DEFAULT_CONFIG);
  const steered = pi.sent.map((s) => String(s.message.content)).join("\n");
  check(getState().phase === "drafting", "stays drafting with no reviewer");
  check(steered.includes("No reviewer available"), "no-runner notice sent");
  check(steered.includes("live user"), "asks for a live user go");
  check(steered.includes("default does not count"), "excludes ask-tool defaults");
  replaceState(IDLE_STATE);
});

function analysisDraftFile(cwd: string): string {
  const content = [
    "## Requirements",
    "- first requirement",
    "- second requirement",
    "",
    "## Evidence",
    "- found in the code",
    "",
    "## Analysis",
    "Root cause sits in alloc.c:77; bound the buffer to fix it.",
    "",
  ].join("\n");
  mkdirSync(join(cwd, ".pi", "quest", "future"), { recursive: true });
  writeFileSync(join(cwd, draftPath(QID)), content);
  return hashContent(content);
}

async function runAnalysisPass(withChild: boolean): Promise<{ phase: string; steered: string }> {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-analyze-"));
  const target = analysisDraftFile(cwd);
  const base = createDraft(createQuest("decide", QID), "decide", "analysis");
  let pre: QuestState = { ...base, draft: { ...base.draft!, planAuthored: true } };
  if (withChild) {
    pre = addChild(pre, {
      qid: "kid001" as Qid,
      brief: "research the alloc path",
      status: "running",
      findings: null,
      acknowledged: false,
    });
  }
  replaceState(pre);
  const pi = busPi();
  const pending = bootDraftReview(pi, fakeCtx(cwd), target, DEFAULT_CONFIG);
  await new Promise((r) => setTimeout(r, 50));
  const request = pi.emitted.find((e) => e.event === "prompt-template:subagent:request");
  check(request !== undefined, "review launched");
  pi.feed({
    requestId: (request!.data as Record<string, unknown>)["requestId"],
    status: "completed",
    result: { kind: "text", text: "VERDICT: PASS\nSEVERITY: NONE\n" },
  });
  await pending;
  const steered = pi.sent.map((s) => String(s.message.content)).join("\n");
  const phase = getState().phase;
  replaceState(IDLE_STATE);
  return { phase, steered };
}

Deno.test("analysis PASS auto-claims straight to validating, skipping implementing", async () => {
  const done = await runAnalysisPass(false);
  check(done.phase === "validating", "auto-claimed to validating");
  check(done.steered.includes("approved analysis milestone"), "milestone announced");
  check(done.steered.includes("on to validation"), "validation named");
});

Deno.test("analysis PASS falls back to a work phase while sub-quests run", async () => {
  const done = await runAnalysisPass(true);
  check(done.phase === "implementing", "falls back while children unfinished");
  check(done.steered.includes("stays provisional while sub-quests run"), "fallback explained");
});
