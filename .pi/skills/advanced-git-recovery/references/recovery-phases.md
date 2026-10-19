# Recovery Phases — Full Edit List from da871cb1

Source: `docs/HIGH_LEVEL_PLAN_CONFIGURABLE_REVIEWER.md` `da871cb1fd5fb73cda525585f409d3bfc80133f0 743 lines Status BUILD B3 1788269285` `Locked decisions: state>env>.pi/settings.json enabled=true hierarchical global→quest §1 Point invisible when working`.

## Phase L — Log Observability Recovery (first, before A/B/C/D)

Re-apply to reach `56 passed (322 steps) DAG 130/688/1 zip 47bf83a1`:

1. `src/logging/types.ts:121 DRAFT_*` add `DRAFT_APPENDED, DRAFT_APPEND_DEDUPED, DRAFT_CONVERSATIONAL_IGNORED, DRAFT_PROMOTED, DRAFT_DISCARDED, SYNTHETIC_FILTERED, CLASSIFICATION_RESULT, REQUIRE_CONFIRM_DECISION, ATTEMPT_INCREMENTED, PENDING_COALESCED_DROPPED, PLAN_REVIEW_SUPPRESSED_MATERIAL_CHANGE, CRITICAL_REVIEW_FORCED` + `172 INITIAL_PROMPT, USER_PROMPT, SEMANTIC_SNAPSHOT, STEP_SUMMARY` + `QuestLogContext` `intentHash/intentLen/slice/elapsedMs/opencodeSessionId/draftPromptsCount/attemptKey/classification/boundaryKey/thought*`.

2. `src/logging/formatters.ts:32 priorityKeys` add `intentHash/opencodeSessionId/draftPromptsCount/attemptKey/classification/boundaryKey`.

3. `src/logging/summary/state.ts` `initialPromptCount/userPromptCount/semanticSnapshotCount` + `reducers.ts:82` `DRAFT_*/SEMANTIC_SNAPSHOT/MUTEX_*` counters.

4. `src/hooks/handlers.ts:43 dedup intentHash/slice+elapsedMs opencodeSessionId + INITIAL_PROMPT once ref=run/initial-prompt.txt` `68 SEMANTIC_SNAPSHOT from→to ≤1/turn gates.ts:157`.

5. `src/hooks/index.ts:249 USER_PROMPT slice/hash/classification vs SYNTHETIC_FILTERED` `422-453 3-case CRITICAL_REVIEW_ORPHAN_CLEARED`.

6. `src/tools/update/executor.ts:97 DRAFT_DISCARDED {hash,reviewId,boundaryKey,questId} future-archive` `342 STATE_UPDATE_ACCEPTED → SEMANTIC_SNAPSHOT`.

7. `src/diagnostic/packaging.ts:98 run/future/*.md+future-archive/*.md` + `verify` `summary.ts:57 manifest opencodeSessionId/startMs/elapsedMax`.

8. `src/persistence.ts:87 future_draft_exists requiredAction=quest_update_state` `src/gates.ts:32 REQUIRE_CONFIRM_DECISION` `utils/mutex.ts:5 hadContention MUTEX_* always` `critical_agent/policy.ts:403 REVIEW_DEDUP_HIT/FIRST_PLAN_REVIEW_ALREADY_FIRED`.

9. `tests/logging_maturity.test.ts 8 steps` `DRAFT_APPENDED/PENDING_COALESCED/ATTEMPT_INCREMENTED/REQUIRE_CONFIRM/SYNTHETIC_FILTERED`.

## Phases A/B/C/D — reuse original

- **A** `gates/policy/executor/hooks` behind `isReviewerEnabled` (`state>env`).
- **B** `firstPlanReviewFired` singleShot `requireConfirm` `gates.ts:32` `executor.ts:202` `pending_coalesce.ts:33`.
- **C** `P1 atomic policy.ts:334 P2 verb-filter checks.ts:36 P3 synthetic 84→14 P4 orphan hooks/index.ts:66`.
- **D** `questId unify tracker.ts:28`.

Verification: `deno run --allow-read scripts/check-pi-quest-dag.ts DAG gate: passed` `deno test --allow-all 56 passed (322) → 57 (323)` `npm --prefix .pi/extensions/pi-quest run zip 47bf83a1 PASSED 218 entries`.
