import { check } from "../check.ts";
import { mkdirSync, mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import { DEFAULT_CONFIG } from "../../src/config.ts";
import { createDraft, createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import type { Qid } from "../../src/domain/qid.ts";
import { draftPath } from "../../src/domain/paths";
import { bootDraftReview, hashContent } from "../../src/drafting/reviews.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

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

async function runPassVerdict(evidence: boolean): Promise<{ phase: string; steered: string }> {
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
  const phase = getState().phase;
  replaceState(IDLE_STATE);
  return { phase, steered };
}

Deno.test("review PASS promotes with recorded research", async () => {
  const done = await runPassVerdict(true);
  check(done.phase === "implementing", "promoted");
  check(done.steered.includes("promoted to implementing"), "promotion announced");
});

Deno.test("review PASS withholds promotion without recorded research", async () => {
  const done = await runPassVerdict(false);
  check(done.phase === "drafting", "stays drafting");
  check(done.steered.includes("needs recorded research"), "withhold explains");
  check(done.steered.includes('"go"'), "go escape offered");
});
