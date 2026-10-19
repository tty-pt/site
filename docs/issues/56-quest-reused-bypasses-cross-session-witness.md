---
id: 56
title: "QUEST_REUSED mount bypasses cross-session filesystem witness — new sessions spawn against in-flight quest"
state: done
severity: high
requires: [37]
validates: "second QUEST_REUSED on same questId while REASSESSMENT_PENDING is refused/coalesced, not mounted as fresh session"
area: "hooks/index.ts, persistence.ts, state.ts, utils/mutex.ts, critical_agent/policy.ts"
parent: 44
---
# Issue: QUEST_REUSED mount bypasses cross-session filesystem witness

- **Area:** `pi-quest` session mount — `hooks/index.ts` (initial prompt / `QUEST_REUSED`), `persistence.ts` (`QUEST_REUSED` / `SAVE_VERIFIED gen1`), `state.ts` (`sessionStates`), `utils/mutex.ts` (`.review.lock` / `.review.active` filesystem witness), `critical_agent/policy.ts` (review-launch witness at `:247/294/447`)
- **Runs observed:** `1788349108` (2026-09-02 11:38–11:45) — 3 sessions on same questId: `01a061e9-8e27-7797-bc99-d8d2364c9d16` (479 lines, main research→revising) plus `01a061ed-9f79-71e3-a43d-a0cfb2cae5ff` (118) and `01a061ed-9daf-77cf-bf96-27e63487c9f5` (120). Each extra session logged `QUEST_REUSED` (`execution.log:231,238`) + `SAVE_VERIFIED gen1 hash=a515d2be` (`:229,236`) + `INITIAL_PROMPT turn0` (`:232,239`) against the already-running quest, then ran unchecked misdirected investigation (see #58).
- **Severity:** High — defeats the global `maxConcurrency=1` per questId invariant that #37/#16 fixed for the review-launch path; amplifies reassessment deadlock and message churn.

## Current behavior

- #37's Option B filesystem witness (`utils/mutex.ts` `REVIEW_LOCK_FILE .review.lock` via `acquireReviewFileLock`, `REVIEW_ACTIVE_FILE .review.active` via `createReviewActiveFile`) is acquired only inside `policy.ts:runCriticalReview`'s hierarchical `withQuestLock(global, quest)` around the review-launch check+register section (`policy.ts:247/294/447`). It correctly suppresses duplicate **review launches** (`CRITICAL_REVIEW_SUPPRESSED_DUPLICATE`), but it does **not** guard the top-level session mount.
- A fresh opencode/pi session can therefore `QUEST_REUSED` the same `questId` (via `persistence.ts` / `hooks/index.ts` initial-prompt handling) and receive `SAVE_VERIFIED gen1` + `INITIAL_PROMPT turn0` even while the quest is `REASSESSMENT_PENDING` or `awaitingReview`. No `GATE_BLOCKED` or coalescing occurs at mount time. The new session gets its own `StoredState` in `state.ts:sessionStates` and proceeds to read/search/write independently.

## Desired behavior

- On `QUEST_REUSED` / initial-prompt for a questId that already has an in-flight session (or a `.review.active` / session-liveness witness on disk), refuse the mount or coalesce it: return the existing session's state instead of creating a new `sessionId` entry, or gate the new session to `research`-only reads until the in-flight reassessment/review completes.
- Keep the fast in-process `questLockChains` path, but add a filesystem liveness witness checked at mount time (e.g. probe `.review.active` / `.review.lock` / a `session.liveness` file under `current/<qid>/`) and log `QUEST_REUSED_COALESCED` / `GATE_BLOCKED` rather than `QUEST_REUSED` + new `TURN_START 0`.
- Ensure stray mounts do not increment `researchRound` / `lastGeneratedSec` / `saveGeneration` for the same quest.

## Manual validation in `pi`

1. Start session A, trigger a `plan_review` (material draft change) so `01a061e9`-like session enters `REASSESSMENT_PENDING` / `awaitingReview`.
2. While A is pending, open a second `pi` session against the same project and send the same quest prompt. Before fix: new `QUEST_REUSED` + `TURN_START 0` appears in `execution.log` with a new `sessionId`. After fix: mount is refused/coalesced with a single log line and no new `TURN_START 0`.

Related: #16, #37, #58, #57.
