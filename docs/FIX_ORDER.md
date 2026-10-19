# Fix order — manually testable in `pi` coding agent

**Goal:** plan is drafted and re-drafted until problems dealt with, without blocking if human approves before agents return. Each step keeps `pi` runnable, `deno test` green, `DAG passed (130 files ~688 edges, 2 allowlisted)`.

Pre: `npm --prefix .pi/extensions/pi-quest run zip` + `deno test --allow-all` + `deno run --allow-read scripts/check-pi-quest-dag.ts` baseline (see `33-verification-matrix.md` 12-row `grep` matrix). Hash helper `computeFileSha256` reused, file-already rule holds.

---

### 1 — 36 `quest_update_state` must not bypass draft approval

- **Files:** `tools/update/executor.ts:syncQuestIdentity` + `executeUpdateStateTool`
- **Gate:** reject when `activeDraft && hasReviewer && !isDraftReviewValid(state)` with `REVIEW_DEDUP_HIT {shard:draft, reason:draft_not_approved}`; keep `#24` archive-before-unlink.
- **Manual test in `pi`:**
  1. Start `pi` single session, reviewer enabled.
  2. Send initial prompt → `future/<slug>.md` created, `activeDraft=<slug>`.
  3. Agent `quest_update_state` (same slug) → before fix it promotes to `current/<qid>/quest.md`; after fix it returns `Draft not yet reviewer-approved — boundaryKey draft:slug:hash` steer, stays in drafting, `execution.log:REVIEW_DEDUP_HIT draft_not_approved` + `CLASSIFICATION_RESULT`.
  4. Follow-up refinement before agent approval must append to `future/<slug>.md`, not promote.

---

### 2 — 37 cross-session mutex as filesystem witness (keep RAM promise chain)

- **Files:** `utils/mutex.ts` + `policy.ts:runCriticalReview` hierarchical `global:review:sessionId` → `quest:slug:sessionId` + `tracker.ts:activeReviews` + `pending_coalesce.ts:dequeuePendingIfNeeded`
- **Change:** add `.review.lock` per `questId` (`current/<qid>/.review.lock` `O_EXCL`) around the synchronous `GLOBAL_REVIEW_CAP`/`canLaunchReview`/`registerActiveReview` section; keep promise chain intra-session; replace `await import` swallow with static `import {logEvent}` leaf — `#23`.
- **Manual test:**
  1. Open 2 `pi` sessions same `questId`.
  2. In A trigger `plan_review` (refinement while draft). See `MUTEX_ACQUIRED holdMs` + `⏸ Awaiting plan_review`.
  3. While `starting|running`, in B trigger another review same `questId`. Before fix B starts second reviewer, `researchRound` bump. After fix B gets `GLOBAL_REVIEW_CAP_HIT` / `CRITICAL_REVIEW_COALESCED` + `PENDING_COALESCED_RESOLVED` queued, only one `activeReviews` entry, `MUTEX_WAIT hadContention:true`.

---

### 3 — 38 draft follow-up must invalidate stale `plan_review`

- **Files:** `hooks/index.ts:draft branch` (`REFINEMENT|QUESTION` → `appendToFutureDraft` → `draftLastSavedHash`) + `snapshot.ts:isReviewSnapshotCurrent` + `reconcile.ts:superseded` + `pending_coalesce.ts`
- **Change:** after hash update, if `awaitingReview.kind==plan_review` or `findActiveReviewForQuest(slug)` and new `draft:slug:newHash != snapshot.boundaryKey`, mark supersede-candidate (check `draftLastReviewKey` too) + `setPendingReview` with new key; `rejected.ts:52` already suppresses stale obligation; ensure `draft-prompts.jsonl` rehydrated on restart — `#26`.
- **Manual test:**
  1. Initial prompt → draft `plan_review` `⏸ Awaiting`.
  2. While running, send refinement `“also handle X”`. Before fix two `starting` reviews, stale may `APPROVE` old hash. After fix log `PENDING_COALESCED_DROPPED staleCount` then `RESOLVED draft:slug:newHash`, old completes `superseded:true` (no `APPROVE`), pending runs once on new hash. Second rapid refinement coalesces.

---

### 4 — 39 human early `go` non-blocking but re-confirm required after `REVISE`

- **Files:** `classification.ts:acceptRootConfirmation` + `gates.ts:canImplement` (`awaitingUserConfirmation` vs `awaitingReview` vs `isPlanReviewValidForState`) + `research.ts:triggerReassessment` + `reconcile/rejected.ts:40` + `executor.ts:maybeTriggerPlanReview`
- **Change:** make confirmation per-boundary (`confirmedBoundaryKey` or clear `confirmedQuests` when `REVISE` changes `boundaryKey`); `triggerReassessment` and draft-hash-change clear stored `go`; `maybeTriggerPlanReview isRevisionAfterRejection` then requires fresh `go`.
- **Manual test — two sub-tests:**
  - **A human-early:** `go` before agent returns → `CONFIRMATION_RECEIVED` + `confirmedQuests` append but status stays `⏸ Awaiting plan_review` (`AWAITING_REVIEW` blocks, `canImplement false`). Agent `REVISE` → `REASSESSMENT_REQUIRED` and prior `go` cleared → must `go` again. Single early `go` must NOT satisfy second cycle.
  - **B agent-early:** wait `APPROVE` → `Present finalized plan... await explicit "go"` → `go` → `GATE_OPENED` → `canImplement true` (`promoteDraft` or active `write` allowed). One `go` suffices when no intervening `REVISE`.

---

### 5 — Then 20/25/26/27 greppability (keeps `pi-quest-bundle.zip` contract)

- **Files:** `hierarchy/resolve.ts` `filteredCount` etc. + `resolve.ts:futureCount` + `reconstruction.ts:orphan` + `reconstruction.ts:isStoredState`
- **Validate:** `npm --prefix .pi/extensions/pi-quest run zip` → `current-run/manifest.txt 9 fields` + `current-run/future/*.md` + `future-archive` + `compaction-resume.txt` + `draft-prompts.jsonl`; `readdirSync(FUTURE_DIR)` fallback; `rg isStoredState` guarded. See `33-verification-matrix.md`.

**DAG + tests after each step:** `deno run --allow-read scripts/check-pi-quest-dag.ts` stays `passed`; `deno test --allow-all` `54→62`; `unzip -l | rg current-run/(future|future-archive|compaction-resume|draft-prompts)`.

**Dependency note:** 36 before 37 (draft gate already enforced); 38 needs 36+37; 39 needs 36+38; 5 needs 1-4 green (see `34-rollout-order.md`).

Related: `30-epics-a-b-c-d`, `31-obligations`, `32-three-dir-invariant`, `33-verification-matrix`, `35-archive-map`.
