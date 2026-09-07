import { check } from "../check.ts";
import { existsSync, mkdtempSync } from "node:fs";
import { mkdir, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import {
  claimComplete,
  createDraft,
  createQuest,
  IDLE_STATE,
  promote,
} from "../../src/domain/quest.ts";
import { draftPath, stageDir } from "../../src/domain/paths.ts";
import type { Qid } from "../../src/domain/qid.ts";
import type { ParsedReview } from "../../src/review/verdicts.ts";
import { implementationFingerprint } from "../../src/review/flow.ts";
import { buildConclusionSummary, concludeValidationPass } from "../../src/validation/flow.ts";
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
  return { verdict: "PASS", findings: "meets the plan", severity: "NONE", advisories };
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
