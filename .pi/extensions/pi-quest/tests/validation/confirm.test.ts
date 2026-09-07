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
import { implementationFingerprint } from "../../src/review/flow.ts";
import { archiveActiveQuest, archiveTool } from "../../src/surface/tools/archive.ts";
import { applyUpdate } from "../../src/surface/tools/update-state.ts";
import { handleConfirmInput } from "../../src/validation/flow.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

function tmp(): string {
  return mkdtempSync(join(tmpdir(), "pi-quest-confirm-"));
}

function authored() {
  const s = createDraft(createQuest("req", "abc123"), "thing");
  return { ...s, draft: { ...s.draft!, planAuthored: true } };
}

function validating() {
  return claimComplete({ ...promote(authored(), "review"), phase: "implementing" as const });
}

// A plan-review PASS targets the old plan hash — never the implementation
// fingerprint. This is the 1x3BIe shape: approved plan, unvalidated work.
function validatingWithStalePlanPass() {
  return {
    ...validating(),
    lastReview: {
      verdict: "PASS" as const,
      target: "53a063121c2600a62874865a1d08e270005cc483aa59271d547d08b57bcfea34",
      findings: "plan ok",
    },
  };
}

async function withDraftFile(cwd: string): Promise<void> {
  await mkdir(join(cwd, ".pi/quest/future"), { recursive: true });
  await writeFile(join(cwd, draftPath("abc123" as Qid)), "draft", "utf8");
}

Deno.test("user CONFIRM archives COMPLETED when no validator is available", async () => {
  const cwd = tmp();
  const pi = fakePi(); // no tools registered: reviewer unavailable
  replaceState(validatingWithStalePlanPass());
  await withDraftFile(cwd);
  const accepted = await handleConfirmInput(pi, fakeCtx(cwd), "CONFIRM");
  check(accepted === true, "CONFIRM accepted");
  // Reaching idle through the COMPLETED gate proves the user acceptance
  // was recorded as the current PASS (the fake exec writes no zip file).
  check(getState().phase === "idle", "quest archived and cleared");
  check(!existsSync(join(cwd, stageDir("abc123" as Qid))), "staging dir removed");
  check(!existsSync(join(cwd, draftPath("abc123" as Qid))), "future draft removed");
  replaceState(IDLE_STATE);
});

Deno.test("quest_archive tool no longer accepts abandoned", async () => {
  replaceState({ ...promote(authored(), "review"), phase: "implementing" as const });
  const out = await archiveTool(fakePi()).execute(
    "1",
    { outcome: "abandoned", summary: "changed mind" },
    undefined,
    undefined,
    fakeCtx(tmp()),
  );
  const text = String(out.content[0].text ?? "");
  check(text.includes("completed|failed"), "rejection names the agent outcomes");
  check(getState().qid === "abc123", "quest stays active after rejection");
  replaceState(IDLE_STATE);
});

Deno.test("archive FAILED without a current verdict is rejected", async () => {
  const cwd = tmp();
  const pi = fakePi();
  replaceState(validatingWithStalePlanPass());
  let detail = "";
  try {
    await archiveActiveQuest(pi, fakeCtx(cwd), "FAILED", "giving up");
  } catch (err) {
    detail = err instanceof Error ? err.message : String(err);
  }
  check(detail.includes("validation"), "rejection names validation");
  check(detail.includes("continueWork") || detail.includes("CONFIRM"), "rejection points a way forward");
  check(getState().qid === "abc123", "quest stays active after rejection");
  replaceState(IDLE_STATE);
});

Deno.test("archive FAILED with a FAIL verdict on the current work succeeds", async () => {
  const cwd = tmp();
  const pi = fakePi();
  const state = validating();
  replaceState({ ...state, lastReview: { verdict: "FAIL", target: implementationFingerprint(state), findings: "broken" } });
  await withDraftFile(cwd);
  const done = await archiveActiveQuest(pi, fakeCtx(cwd), "FAILED", "unfixable");
  check(done.archivedQid === "abc123", "failed archive recorded");
  check(getState().phase === "idle", "archive clears");
  replaceState(IDLE_STATE);
});

Deno.test("continueWork demotes validating back to implementing", async () => {
  const pi = fakePi();
  replaceState(validatingWithStalePlanPass());
  const done = await applyUpdate(pi, fakeCtx(tmp()), { continueWork: true });
  check(done.error === undefined, "demote accepted");
  check(getState().phase === "implementing", "back to implementing");
  replaceState(IDLE_STATE);
});
