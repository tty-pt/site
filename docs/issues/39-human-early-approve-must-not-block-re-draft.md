---
id: 39
title: "Human early go must not block re-draft, but must require re-confirm after REVISE"
state: ready
severity: medium
requires: [36, 37, 38]
blocked_by: []
validates: "pi: early go before agent => stored but REVISE clears, needs re-go (two sub-tests)"
area: "39-human-early-approve-must-not-block-re-draft.md"
---
# Issue: Human early `go` must not block re-draft, but must require re-confirm after `REVISE`

- **Area:** `pi-quest` human confirmation — `classification.ts:acceptRootConfirmation`, `gates.ts:canImplement` (`awaitingUserConfirmation` + `awaitingReview` + `isPlanReviewValidForState`), `research.ts:triggerReassessment`, `critical_agent/policy/reconcile/approved.ts` + `rejected.ts`, `tools/update/executor.ts:maybeTriggerPlanReview` (`isRevisionAfterRejection`, `alreadyApprovedForBoundary`), `state.ts:confirmedQuests` + `lastPlanReviewBoundaryKey` + `draftLastReviewKey`
- **Runs observed:** `pi` manual test — user `go` before agent returns leaves `CONFIRMATION_RECEIVED` but `⏸ Awaiting plan_review` still blocks; later agent `REVISE` does not clear stored confirmation, so second `REVISE`→`APPROVE` cycle implements without second `go`
- **Severity:** Medium — violates “without blocking if I approve before agents return” and “re-drafted until problems with it are dealt with”

## Current behavior

- Human gate `gates.ts:31` `if(isRootQuest(s) && s.awaitingUserConfirmation) return false` and agent gate `gates.ts:25` `if(hasReviewer && !isPlanReviewValidForState(s)) return false` + `gates.ts:28` `if(awaitingReview.kind==plan_review|final_acceptance) return false` are independent scalars. `tool_gating.ts:222` enforces `awaitingReview` as `GATE_BLOCKED AWAITING_REVIEW` (only `read/research/interaction` + `quest_mark_saved` allowed, blocks `quest_update_state`/`write`).
- `hooks/index.ts:341` active `CONFIRMATION` → `acceptRootConfirmation` (`classification.ts:80`): if `!awaitingUserConfirmation` return, else `awaitingUserConfirmation=false`, push `confirmedQuests`, `syncImplementationPermission`, `persist`, then `checkAndTriggerDirectionReview`. Draft `CONFIRMATION` (`hooks/index.ts:261`) requires `isDraftReviewValid` when `hasReviewer` else rejects with `CONFIRMATION_REJECTED` steer.
- `acceptRootConfirmation` is called without checking `awaitingReview` or `isPlanReviewValidForState` — so early `go` while `awaitingReview=plan_review` is stored (`confirmedQuests += slug`, `awaitingUserConfirmation=false`) but `canImplement` stays false via `awaitingReview`/`!isPlanReviewValid` — `GATE_OPENED confirmation_accepted` optimism suppressed because `isImplementable` still false.
- `research.ts:triggerReassessment` (active quest refinement, `classification.ts:228`, `compaction/recovery.ts:80`, `rejected.ts:69`) clears `awaitingUserConfirmation=false` and `confirmedQuests = filter(active)` for active quests — but **draft path never calls it**, and **active `REVISE` path** (`critical_agent/policy/reconcile/rejected.ts:40`) only does `lastPlanReviewApproval=null` + `triggerReassessment` only on `CRITICAL|MAJOR`, not on `plan_review REVISE` (which queues obligation + steer but leaves `confirmedQuests` intact if prior `triggerReassessment` not run).
- `executor.ts:283` `maybeTriggerPlanReview` on boundary computes `isRevisionAfterRejection = priorReviewRejected && isMaterialPlanChange` (`priorReviewRejected` via `latestCompletedStatus==rejected`), `alreadyApprovedForBoundary = isPlanReviewValid && lastPlanReviewBoundaryKey==postKey`, `shouldTriggerReview = !alreadyApproved && !alreadyRequested && (initial|firstDraft|materialChange|revisionAfterRejection)`. `alreadyApproved` prevents re-review if already `APPROVE` on that boundary, but human confirmation is **not per-boundary** — a prior `go` for `boundaryKey=oldHash` still satisfies `awaitingUserConfirmation==false` for `newHash`.
- Result: human early `go` does not unblock (good), but after agent `REVISE` the stored `go` is not reliably cleared, so a second `APPROVE` can implement without second human confirmation — violates re-draft loop.

## Desired behavior

- Make human confirmation **per-boundary** (or clear on invalidation):
  - Store confirmation against `boundaryKey` that was confirmed: `confirmedBoundaryKey` (or compare `confirmedQuests` with `lastPlanReviewBoundaryKey`/`draftLastReviewKey`). Simplest: on `triggerReassessment` **and** on any `REVISE` that changes `boundaryKey`, clear the confirmation for that quest: `state.confirmedQuests = state.confirmedQuests.filter(q => q !== state.active)` and `state.awaitingUserConfirmation = true` if `isRootQuest` and `!isPlanReviewValidForState` (or keep false but require fresh check via `isRootQuest && !confirmedForBoundary`).
  - `gates.ts:31` and `getImplementationBlockReason:138` should check `isRootQuest && awaitingUserConfirmationForBoundary` not just scalar — i.e., `confirmedForBoundary = state.confirmedQuests.includes(active) && state.confirmedBoundaryKey == currentBoundaryKey` (new field or reuse `lastPlanReviewBoundaryKey` equality).
  - Minimal change: on `rejected.ts:40` `lastPlanReviewApproval=null`, also clear `confirmedQuests` for that slug; on `hooks/index.ts:draft refinement` that changes `draft:slug:hash`, clear `confirmedQuests` for draft.
- Keep non-blocking: early `go` while `awaitingReview` stays stored (`confirmedQuests` + `awaitingUserConfirmation=false`) but `canImplement` remains `false` until `isPlanReviewValidForState` true (agent `APPROVE` sets `lastPlanReviewApproval` with matching `boundaryKey` via `approved.ts:38`). Once `APPROVE` arrives, `syncImplementationPermission` opens gate without requiring second `go` **unless** a `REVISE` intervened — then the stored `go` was cleared and a fresh `go` is required.
- Add logs: already `CLASSIFICATION_RESULT`, `REQUIRE_CONFIRM_DECISION`, `PLAN_REVIEW_SUPPRESSED_MATERIAL_CHANGE` etc. — ensure early `go` while `awaitingReview` emits `AWAITING_REVIEW` block reason not `CONFIRMATION_PENDING`, so stale `go` is auditable.

## Manual validation in `pi` (step 4 — two sub-tests)

- **Test A human-early (must not block re-draft):**
  1. Initial prompt → draft `plan_review` `awaitingReview` shown.
  2. You send `go` **before** agent returns. Expect `CLASSIFICATION_RESULT CONFIRMATION` + `CONFIRMATION_RECEIVED` + log `confirmedQuests` append, but status stays `⏸ Awaiting plan_review ... No writes until verdict` (`tool_gating AWAITING_REVIEW`) — not `PLAN_REVIEW_PENDING`. `canImplement` still false.
  3. Agent returns `REVISE` (from draft or active `plan_review`). Expect `rejected.ts` clears `lastPlanReviewApproval`, `triggerReassessment` or draft hash change clears stored confirmation, status becomes `REASSESSMENT_REQUIRED` or new `⏸ Awaiting plan_review` with **cleared** confirmation. You must send `go` again — single early `go` does not satisfy second cycle.

- **Test B agent-early (must be re-confirmable):**
  1. Same initial prompt, wait for agent `APPROVE` → message `Present finalized plan to user now and await explicit "go"` (`policy.ts:428`).
  2. You send `go` → `acceptRootConfirmation` → `GATE_OPENED` → `canImplement true` (for draft, `promoteDraft` succeeds; for active, `write`/`edit` allowed). One `go` suffices when no intervening `REVISE`.

- **Negative:** without fix, Test A’s early `go` persists through `REVISE` and second `APPROVE` implements without second `go`.

Related: #13, #21, #30, #36, #38, #33.
