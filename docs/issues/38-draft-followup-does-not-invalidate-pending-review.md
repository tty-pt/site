---
id: 38
title: "Draft follow-up does not invalidate pending plan_review — stale boundary runs to verdict"
state: done
severity: medium
requires: [36, 37]
validates: "pi: refinement while awaitingReview plan_review => superseded + PENDING_COALESCED_RESOLVED new hash"
area: "hooks/index.ts:before_agent_start, REFINEMENT|QUESTION, paths.ts:appendToFutureDraft"
parent: 43
---
# Issue: Draft follow-up does not invalidate pending `plan_review` — stale boundary runs to verdict

- **Area:** `pi-quest` hooks/follow-up — `hooks/index.ts:before_agent_start` draft branch (`REFINEMENT|QUESTION`), `paths.ts:appendToFutureDraft`, `state.ts:draftLastSavedHash`, `critical_agent/policy.ts:isDraftReviewValid`, `critical_agent/snapshot.ts:isReviewSnapshotCurrent`, `critical_agent/policy/pending_coalesce.ts:dequeuePendingIfNeeded`, `critical_agent/policy/launch_guard.ts:checkLaunchGuard`, `reconstruction.ts:orphan`
- **Runs observed:** Draft refinement while `awaitingReview.kind==plan_review` leaves old `draft:slug:oldHash` review running; new `draft:slug:newHash` queued separately and both may log `APPROVE` — manual `pi` follow-up during drafting
- **Severity:** Medium — stale `APPROVE` can promote outdated `future/<slug>.md` hash; violates “re-drafted until problems dealt with”

## Current behavior

- Draft refinement path `hooks/index.ts:286-331`:
  - pushes `draftPrompts`, appends `draft-prompts.jsonl`, calls `appendToFutureDraft(activeDraft, trimmed)` (file dedup on `slice(0,80)`), recomputes `draftLastSavedHash = sha256(fileContent).slice(0,12)` (or `hash(trimmed)` fallback), logs `DRAFT_APPENDED`/`DEDUPED`, `persist`, then if `draftPrompts.length>=2 && !isDraftReviewValid` fire-and-forgets `checkAndTriggerPlanReview` deduped via `__lastDraftReviewKey`.
  - **Does not** call `triggerReassessment` (`research.ts:202` — active-quest only), does not clear `awaitingReview`, does not touch `pendingReviews`.
- `isDraftReviewValid(s)` checks live file hash `draft:slug:hash == approval.boundaryKey || == draftLastReviewKey`. After refinement, live hash `newHash` != `oldHash`, so a running review with `snapshot.boundaryKey=draft:slug:oldHash` is stale but `isReviewSnapshotCurrent` for draft checks `snapshot.boundaryKey != currentState.lastPlanReviewBoundaryKey` (`snapshot.ts:114`) — for draft, `lastPlanReviewBoundaryKey` is not `draftLastReviewKey`, so it stays `current:true`.
- `canLaunchReview` (`tracker.ts:128`) sees `inCriticalReview`/`activeReviews` and returns `blocked`; `checkLaunchGuard` does `setPendingReview({kind:plan_review, boundaryKey:newHash, planVersion, stateHash})` + `CRITICAL_REVIEW_COALESCED`. `pending_coalesce.ts:dequeue` only runs after review finishes (`background.ts:onPending` under `withQuestLock(global,quest)`), not proactively.
- Result: old review can `APPROVE` with `boundaryKey=oldHash`, `reconcile/approved.ts` sets `lastPlanReviewApproval.boundaryKey=oldHash`, but live file is already `newHash` — `isDraftReviewValid` then fails anyway, requiring another review, but the stale `APPROVE` still emitted `RESUME_DIRECTIVE_SENT` and `CRITICAL_REVIEW_FORCED` noise.
- Also: `hooks/index.ts:draft-prompts.jsonl` is done but never rehydrated on restart (B′ gap, #26), so follow-up history can be lost if draft not rehydrated.

## Desired behavior

- After `appendToFutureDraft` + `draftLastSavedHash` update, if `state.awaitingReview?.kind=="plan_review"` or `findActiveReviewForQuest(slug)` exists:
  - Mark running review as supersede-candidate: the next `isReviewSnapshotCurrent` must compare against `draftLastReviewKey` and live `draft:slug:hash` (not just `lastPlanReviewBoundaryKey`). If mismatch, `reconcile.ts:55` will set `superseded:true`, clear `awaitingReview` if `reviewId` matches, and suppress obligation via `rejected.ts:52`.
  - Eagerly enqueue the new boundary: `setPendingReview(slug, {kind:plan_review, boundaryKey:"draft:slug:newHash", ...})` with `PENDING_COALESCED_DROPPED` for stale candidates (see #21 for zero-candidate logging) and `PENDING_COALESCED_RESOLVED` for the newest.
- Keep file-already rule: when markdown already in `run/future/*.md`, log only `hash slice(0,12)` + `ref`.
- Ensure `reconstruction.ts:orphan` rehydrates `draftPrompts` from `draft-prompts.jsonl` or live file so follow-up history survives `pi` kill — see #26.

## Manual validation in `pi` (step 3)

1. Start `pi`, send initial prompt → draft `future/<slug>.md` created, `checkAndTriggerPlanReview` fires (status `⏸ Awaiting plan_review`).
2. While that review is `starting|running`, send a refinement `“also handle X”` (classification `REFINEMENT_OR_REQUIREMENT`). Before fix: `execution.log` shows two `plan_review` entries both `starting`, old one may `APPROVE` with stale hash. After fix: log shows `PENDING_COALESCED_DROPPED staleCount` then `RESOLVED` with `boundaryKey draft:slug:newHash`, old review completes as `superseded:true` (no `APPROVE`), pending dequeues and runs once against new hash.
3. Add a second rapid refinement — it should coalesce, not queue two.

Related: #06, #21, #25, #26, #36, #37, #33.
