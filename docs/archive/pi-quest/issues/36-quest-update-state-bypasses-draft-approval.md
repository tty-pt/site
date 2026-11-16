---
id: 36
title: "quest_update_state bypasses draft approval — syncQuestIdentity clears activeDraft without isDraftReviewValid"
state: done
severity: high
requires: []
validates: "pi: quest_update_state while activeDraft && !isDraftReviewValid => rejected draft_not_approved"
area: "tools/update/executor.ts:syncQuestIdentity, executeUpdateStateTool, commands/promote.ts:isDraftReviewValid"
parent: 43
---
# Issue: `quest_update_state` bypasses draft approval — `syncQuestIdentity` clears `activeDraft` without `isDraftReviewValid`

- **Area:** `pi-quest` lifecycle/gating — `tools/update/executor.ts:syncQuestIdentity` + `executeUpdateStateTool`, `commands/promote.ts:isDraftReviewValid`, `hooks/index.ts:before_agent_start` `CONFIRMATION`, `gates.ts:canImplement`, `critical_agent/policy.ts:isDraftReviewValid`, `state.ts:StoredState`
- **Runs observed:** `1788280759` + manual `pi` drafting (agent proceeds without either agent or human approval)
- **Severity:** High — drafting phase skipped, re-draft loop never entered; root cause of “agent goes through drafting without approvals”

## Current behavior

- `hooks/index.ts:before_agent_start` creates `activeDraft` via `shouldStartPersistentQuest` → `createFutureDraftFromPrompt` (`future/<slug>.md`, `draftPrompts`, `pendingRootQuest`, `draftLastSavedHash`). Human `CONFIRMATION` there correctly gates on `hasReviewer ? isDraftReviewValid(state) : true` and on failure sends `CONFIRMATION_REJECTED` + `checkAndTriggerPlanReview` (requires `draft:slug:hash` boundary).
- `commands/promote.ts:promoteDraft` also gates on `isDraftReviewValid` (`approval.boundaryKey == draft:slug:hash(live file) || == draftLastReviewKey`) before `writeFile` + archive.
- **But** `tools/update/executor.ts:syncQuestIdentity` (called unconditionally from `executeUpdateStateTool` when agent calls `quest_update_state`) does:

  ```ts
  state.activeDraft = null; state.draftPrompts = []; state.draftLastSavedHash = null;
  // archive future/slug.md → current/<qid>/future-archive/slug.md
  state.active = targetName; // etc.
  ```

  No call to `isDraftReviewValid`, no `hasReviewer` check, no `REVIEW_DEDUP_HIT` log. `gates.ts:canImplement` draft block `if(activeDraft) return false` lifts as soon as `activeDraft` is nulled, and `tool_gating.ts` allows `quest_update_state` even when `RESEARCH_PENDING`/`PLAN_REVIEW_PENDING`.
- Outside gap: `hooks/index.ts:auto-draft` can be followed immediately by `quest_update_state({researchComplete:true})` before ever running `checkAndTriggerPlanReview` — no investigation receipts required for draft.

## Desired behavior

- `syncQuestIdentity` (and `executeUpdateStateTool` before it mutates) must reject when `state.activeDraft != null && hasReviewer && !isDraftReviewValid(state)` with `success:false` message `Draft not yet reviewer-approved — present plan via future/slug.md and await plan_review APPROVE (boundaryKey draft:slug:hash)` and `logEvent REVIEW_DEDUP_HIT` with `{shard:"draft", reason:"draft_not_approved", quest:activeDraft, hash:slice(0,12)}`. Allow only the file-already rule hash+ref path, not promotion.
- Keep `existsSync`/`copyFileSync` fallback path from `#24` but after the gate. DAG stays `executor → logging` leaf, +0.
- Stale doc note: `executor.ts:maybeTriggerPlanReview` already sets `lastPlanReviewBoundaryKey = postBoundaryKey` before `requestPlanReview`; suppression case should log `PLAN_REVIEW_SUPPRESSED_MATERIAL_CHANGE` (#13) so re-draft livelock is visible.

## Manual validation in `pi` (step 1 of fix order)

1. Start `pi` with `pi-quest` loaded, single session, reviewer enabled (`subagent` or stub runner).
2. Send initial prompt `TEST-PROMPT` → status shows `activeDraft: <slug>` and `future/<slug>.md` with `Requirements: -` plus draft prompt.
3. Observe agent tool calls — it will attempt `quest_update_state` (or trigger it via simulated tool). Before fix it succeeds and `current/<qid>/quest.md` appears with `pendingRootQuest=false`. After fix it returns `Draft not yet reviewer-approved` steer, stays in drafting, and `execution.log` contains `REVIEW_DEDUP_HIT draft_not_approved` + `CLASSIFICATION_RESULT` + `PENDING_COALESCED` if queued.
4. Send a follow-up refinement before agent approval — it must append to `future/<slug>.md`, not promote.

Related: #04, #06, #18, #21, #22, #24, #27, #33. Blocks all later mutex/invalidation fixes.
