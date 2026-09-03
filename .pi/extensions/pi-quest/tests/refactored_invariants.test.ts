import assert from "node:assert";
import {
  canImplement,
  canToolExecuteInCriticalReview,
  type CompactionTransaction,
  createAgentObligation,
  createDefaultState,
  type CriticalReviewState,
  isCriticalReviewValidForCompletion,
  isObligationCurrent,
  type StoredState,
} from "../index.ts";

Deno.test("Invariant 1: failed compaction transaction cannot be treated as completed", () => {
  const state: StoredState = createDefaultState();
  state.active = "test-quest";
  state.questId = "tx123";
  state.saveCount = 5;

  const failedTx: CompactionTransaction = {
    id: "cmp_failed_1",
    phase: "failed",
    activeQuest: "test-quest",
    reason: "normal-compaction",
    stack: ["test-quest"],
    researchRound: 1,
    reassessmentVersion: 0,
    planVersion: 1,
    createdAt: Date.now(),
    failedAt: Date.now(),
    error: "Context compact worker crashed",
  };

  state.activeTransaction = failedTx;

  // Implementation must remain blocked when transaction is in failed state
  assert.strictEqual(
    canImplement(state),
    false,
    "Implementation must be blocked on failed compaction transaction",
  );
});

Deno.test("Invariant 2: stale agent obligations are superseded and not deliverable", () => {
  const state: StoredState = createDefaultState();
  state.active = "test-quest";
  state.questId = "tx123";
  state.saveCount = 5;
  state.researchComplete = true;
  state.reassessmentRequired = false;
  state.resolvedReassessmentVersion = 2;
  state.reassessmentVersion = 2;

  // 1. Stale research obligation when research is already complete
  const researchObligation = createAgentObligation(state, {
    kind: "error",
    code: "RESEARCH_REQUIRED",
    message: "Research is required",
  });
  assert.strictEqual(
    isObligationCurrent(researchObligation, state),
    false,
    "Research obligation must be obsolete when research is complete",
  );

  // 2. Stale reassessment obligation when reassessment is resolved
  const staleReassessmentObligation = createAgentObligation(state, {
    kind: "reassessment",
    code: "REASSESSMENT_REQUIRED",
    message: "Reassessment is pending",
  });
  staleReassessmentObligation.reassessmentVersion = 1; // older than resolved version 2
  assert.strictEqual(
    isObligationCurrent(staleReassessmentObligation, state),
    false,
    "Old reassessment obligation must be superseded when resolved",
  );

  // 3. Current obligation with matching state
  const freshObligation = createAgentObligation(state, {
    kind: "custom",
    code: "CUSTOM_NOTE",
    message: "Important note",
  });
  assert.strictEqual(
    isObligationCurrent(freshObligation, state),
    true,
    "Fresh obligation must be current",
  );
});

Deno.test("Invariant 3: critical review PASS is invalidated when planVersion or saveHash changes", () => {
  const state: StoredState = createDefaultState();
  state.active = "test-quest";
  state.questId = "rev_quest";
  state.planVersion = 2;
  state.saveCount = 3;
  state.lastSavedHash = "hash_v2_saved";
  state.dirty = false;

  const validReview: CriticalReviewState = {
    id: "rev_1",
    questId: "rev_quest",
    kind: "final_acceptance",
    reviewedStateVersion: {
      planVersion: 2,
      saveHash: "hash_v2_saved",
      saveCount: 3,
    },
    verdict: "PASS",
    severity: "NONE",
    findings: [],
    requiredActions: [],
    resolved: true,
    timestamp: Date.now(),
  };

  state.lastCriticalReview = validReview;
  assert.strictEqual(
    isCriticalReviewValidForCompletion(state),
    true,
    "Review must be valid for exact matching state",
  );

  // Invalidate: new plan revision
  state.planVersion = 3;
  assert.strictEqual(
    isCriticalReviewValidForCompletion(state),
    false,
    "Review must be invalid when plan version advances",
  );

  // Restore plan version but mutate save hash
  state.planVersion = 2;
  state.lastSavedHash = "hash_v2_modified";
  assert.strictEqual(
    isCriticalReviewValidForCompletion(state),
    false,
    "Review must be invalid when save hash changes",
  );

  // Restore hash but mark state dirty
  state.lastSavedHash = "hash_v2_saved";
  state.dirty = true;
  assert.strictEqual(
    isCriticalReviewValidForCompletion(state),
    false,
    "Review must be invalid when state is dirty",
  );
});

Deno.test("Invariant 4: critical reviewer is strictly read-only and cannot execute mutating tools", () => {
  assert.strictEqual(
    canToolExecuteInCriticalReview("edit", { path: "src/file.ts" }),
    false,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("write", { path: "src/file.ts" }),
    false,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("user_edit", { path: "src/file.ts" }),
    false,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("user_write", { path: "src/file.ts" }),
    false,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("quest_update_state", {}),
    false,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("quest_archive", {}),
    false,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("bash", { command: "rm -rf foo" }),
    false,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("bash", {
      command: "git commit -m 'change'",
    }),
    false,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("bash", { command: "npm install foo" }),
    false,
  );

  // Read-only operations allowed
  assert.strictEqual(
    canToolExecuteInCriticalReview("read", { path: "src/file.ts" }),
    true,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("doc_to_md", { path: "doc.pdf" }),
    true,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("search_graph", { name_pattern: ".*" }),
    true,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("search_code", { query: "foo" }),
    true,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("bash", { command: "git status" }),
    true,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("bash", { command: "git diff" }),
    true,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("bash", { command: "ls -la src" }),
    true,
  );
  assert.strictEqual(
    canToolExecuteInCriticalReview("bash", { command: "wc -l src/types.ts" }),
    true,
  );
});
