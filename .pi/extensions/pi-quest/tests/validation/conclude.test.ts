import { check } from "../check.ts";
import { existsSync, mkdirSync, mkdtempSync, writeFileSync } from "node:fs";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import {
  createQuest,
  IDLE_STATE,
} from "../../src/domain/quest.ts";
import {
  claimComplete,
  createDraft,
  promote,
  promoteToValidation,
} from "../../src/domain/transitions.ts";
import { draftPath, stageDir } from "../../src/domain/paths.ts";
import type { Qid } from "../../src/domain/qid.ts";
import type { ParsedReview } from "../../src/review/verdicts.ts";
import { implementationFingerprint } from "../../src/review/flow.ts";
import { buildConclusionSummary, concludeValidationPass, ensureValidationFlow, repairAfterFail, validationBrief } from "../../src/validation/flow.ts";
import { applyUpdate } from "../../src/surface/tools/update-state.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

function tmp(): string {
  return mkdtempSync(join(tmpdir(), "pi-quest-conclude-"));
}

function authored() {
  const s = createDraft(createQuest("req", "abc123"), "thing");
  return { ...s, draft: { ...s.draft!, planAuthored: true } };
}

function validating() {
  return claimComplete({ ...promote(authored(), "review"), phase: "implementing" as const });
}

function passReview(advisories: string): ParsedReview {
  return { verdict: "PASS", findings: "meets the plan", severity: "NONE", advisories, text: "" };
}

async function withDraftFile(cwd: string): Promise<void> {
  await mkdir(join(cwd, ".pi/quest/future"), { recursive: true });
  await writeFile(join(cwd, draftPath("abc123" as Qid)), "draft", "utf8");
}

Deno.test("validation PASS concludes and archives automatically", async () => {
  const cwd = tmp();
  const pi = fakePi();
  const state = validating();
  const target = implementationFingerprint(state);
  // runIsolatedReview records the verdict before the PASS branch concludes.
  replaceState({ ...state, lastReview: { verdict: "PASS", target, findings: "meets the plan" } });
  await withDraftFile(cwd);
  const done = await concludeValidationPass(pi, fakeCtx(cwd), "abc123", target, passReview(""), true);
  check(done.archived === true, "concluded");
  check(getState().phase === "idle", "quest archived and cleared");
  check(!existsSync(join(cwd, stageDir("abc123" as Qid))), "staging dir removed");
  check(!existsSync(join(cwd, draftPath("abc123" as Qid))), "future draft removed");
  replaceState(IDLE_STATE);
});

Deno.test("conclusion skips when work changed during validation", async () => {
  const cwd = tmp();
  const pi = fakePi();
  const state = validating();
  const target = implementationFingerprint(state);
  // runIsolatedReview records the verdict before the PASS branch concludes.
  replaceState({ ...state, lastReview: { verdict: "PASS", target, findings: "meets the plan" } });
  await withDraftFile(cwd);
  // Work moves after the verdict: the amendment changes the fingerprint.
  const moved = await applyUpdate(pi, fakeCtx(cwd), { amendment: { change: "extra tweak", reasons: "polish" } });
  check(moved.error === undefined, "amendment recorded");
  check(implementationFingerprint(getState()) !== target, "fingerprint moved");
  const done = await concludeValidationPass(pi, fakeCtx(cwd), "abc123", target, passReview(""), true);
  check(done.archived === false, "stale conclusion skipped");
  check(getState().phase === "validating", "quest still validating");
  check(pi.sent.some((s) => JSON.stringify(s).includes("changed during validation")), "agent steered to re-validate");
  replaceState(IDLE_STATE);
});

Deno.test("auto-archive disabled leaves the quest for manual archiving", async () => {
  const cwd = tmp();
  replaceState(validating());
  await withDraftFile(cwd);
  const done = await concludeValidationPass(fakePi(), fakeCtx(cwd), "abc123", "target", passReview(""), false);
  check(done.archived === false, "not concluded");
  check(getState().phase === "validating", "quest still awaiting archive");
  replaceState(IDLE_STATE);
});

Deno.test("conclusion summary carries counts and advisories", async () => {
  const state = {
    ...validating(),
    amendments: [{ change: "a", reasons: "b" }],
    setbacks: [{ reason: "c", evidence: ["d"] }],
  };
  const text = buildConclusionSummary(state, "abcdef1234567890", passReview("tighten the gate text"));
  check(text.includes("abcdef123456"), "short target named");
  check(text.includes("1 amendment(s)") && text.includes("1 setback(s)"), "work counts named");
  check(text.includes("tighten the gate text"), "advisories preserved");
  const bare = buildConclusionSummary(validating(), "abcdef1234567890", passReview("  "));
  check(bare.includes("(no advisories)"), "empty advisories marked");
});

function analysisValidating() {
  const s = createDraft(createQuest("req", "abc123"), "thing", "analysis");
  return promoteToValidation({ ...s, draft: { ...s.draft!, planAuthored: true } }, "review");
}

function analysisDoc(cwd: string): void {
  mkdirSync(join(cwd, ".pi/quest/future"), { recursive: true });
  writeFileSync(join(cwd, draftPath("abc123" as Qid)), [
    "## Requirements",
    "- requirement",
    "",
    "## Evidence",
    "- traced alloc.c:77",
    "",
    "## Analysis",
    "Root cause sits in alloc.c:77; bound the buffer to fix it.",
    "",
  ].join("\n"));
}

Deno.test("analysis validator material carries the analysis body and transcript extract", async () => {
  const cwd = tmp();
  analysisDoc(cwd);
  const state = analysisValidating();
  const ctx = fakeCtx(cwd, [
    { data: { role: "user", content: "Measure the alloc path." } },
    { data: { role: "assistant", content: "Buffer grows unbounded." } },
  ]);
  const brief = await validationBrief(ctx, state);
  check(brief.kind === "analysis", "kind labeled");
  check(brief.plan.includes("alloc.c:77"), "analysis body is the deliverable");
  check(!brief.plan.includes("## Requirements"), "deliverable only, not the whole doc");
  check(brief.transcriptExtract !== undefined, "transcript extract present");
  check(brief.transcriptExtract!.includes("[assistant] Buffer grows unbounded."), "extract carries the thread");
});

Deno.test("repairAfterFail returns analysis quests to drafting, standard to implementing", () => {
  const analysis = repairAfterFail(analysisValidating());
  check(analysis.phase === "drafting", "analysis repairs to drafting");
  check(analysis.kind === "analysis", "kind survives the demotion");
  const standard = repairAfterFail(validating());
  check(standard.phase === "implementing", "standard repairs to implementing");
  check(standard.kind === "standard", "standard kind unchanged");
});

Deno.test("analysis PASS archives and wakes with the delivered analysis", async () => {
  const cwd = tmp();
  analysisDoc(cwd);
  const pi = fakePi();
  const state = analysisValidating();
  const target = implementationFingerprint(state);
  replaceState({ ...state, lastReview: { verdict: "PASS", target, findings: "analysis grounded" } });
  const done = await concludeValidationPass(pi, fakeCtx(cwd), "abc123", target, passReview(""), true);
  check(done.archived === true, "analysis concluded");
  check(getState().phase === "idle", "archived and cleared");
  check(
    pi.sent.some((s) => JSON.stringify(s).includes("DELIVERED ANALYSIS:") && JSON.stringify(s).includes("alloc.c:77")),
    "conclusion wake carries the delivered analysis excerpt",
  );
  replaceState(IDLE_STATE);
});

Deno.test("analysis validation FAIL returns the quest to drafting and resets the doc", async () => {
  const cwd = tmp();
  analysisDoc(cwd);
  replaceState(analysisValidating());
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
  const pending = ensureValidationFlow(pi, fakeCtx(cwd, []));
  const deadline = Date.now() + 4000;
  let request = undefined;
  while (Date.now() < deadline) {
    request = emitted.find((e) => e.event === "prompt-template:subagent:request");
    if (request !== undefined) break;
    await new Promise((r) => setTimeout(r, 25));
  }
  check(request !== undefined, "validation review launched");
  const feed = (data: unknown) => {
    for (const handler of handlers.get("prompt-template:subagent:response") ?? []) handler(data);
  };
  feed({
    requestId: (request!.data as Record<string, unknown>)["requestId"],
    status: "completed",
    result: { kind: "text", text: "VERDICT: FAIL\nSEVERITY: MAJOR\nFINDINGS:\n- Issue: ungrounded claim\n  Evidence: nothing in the repo\n\nREQUIRED REVISIONS:\n- trace the alloc path\n" },
  });
  await pending;
  check(getState().phase === "drafting", "returned to drafting, not implementing");
  check(getState().kind === "analysis", "kind preserved");
  const doc = await readFile(join(cwd, draftPath("abc123" as Qid)), "utf8");
  check(doc.includes("drafting"), "doc status reset to drafting");
  check(
    pi.sent.some((s) => JSON.stringify(s).includes("Address the findings in the analysis")),
    "analysis-flavored FAIL wake",
  );
  replaceState(IDLE_STATE);
});
