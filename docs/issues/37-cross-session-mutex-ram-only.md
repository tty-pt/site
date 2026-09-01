---
id: 37
title: "Cross-session mutex is RAM-only — no global maxConcurrency=1 per questId"
state: ready
severity: high
requires: [36]
blocked_by: []
validates: "pi: 2 sessions same quest => 1 active, second GLOBAL_REVIEW_CAP_HIT + coalesced"
area: "37-cross-session-mutex-ram-only.md"
---
# Issue: Cross-session mutex is RAM-only — no global `maxConcurrency=1` per `questId`

- **Area:** `pi-quest` critical-agent concurrency — `utils/mutex.ts:questLockChains` + `getQuestLockKey`/`getGlobalReviewLockKey`, `critical_agent/policy.ts:runCriticalReview` hierarchical lock, `critical_agent/tracker.ts:activeReviews` + `pendingReviews` + `canLaunchReview`, `critical_agent/policy/launch_guard.ts:checkLaunchGuard`, `critical_agent/policy/pending_coalesce.ts:dequeuePendingIfNeeded`, `state.ts:sessionStates` + `sessionStartMap`, `constants.ts:GLOBAL_REVIEW_CAP`
- **Runs observed:** `1788280759` at `16:41:23` — 4 sessions `01a05dd6`, `01a05dd8-c2e6`, `01a05dd8-c3a5`, `01a05dd9-*` on same `questId 1788280759` each `SAVE_STARTED SAVE_VERIFIED gen1` + `QUEST_REUSED` + `TURN_START reassessment T0` while `01a05dd6` already `REASSESSMENT_PENDING round4`, each increments `researchRound 4→5→6` and floods `NO_PROGRESS`/`periodic_checkpoint` (20+ emissions) — `#16`
- **Severity:** High — concurrent reviewers amplify reassessment loop, violate single-flight drafting

## Current behavior

- `utils/mutex.ts` is `Map<string,Promise<void>> questLockChains` promise chain (`withQuestLock(key, fn): await prev; ... release; delete if chain still current`). Keys are `quest:<slug>:<sessionId>` and `global:review:<sessionId>` (`getQuestLockKey`, `getGlobalReviewLockKey`). Logs `MUTEX_WAIT`/`MUTEX_ACQUIRED` via `await import` swallow — `#23`.
- `policy.ts:runCriticalReview` does hierarchical `withQuestLock(globalLockKey, withQuestLock(questLockKey, ...))` then inside checks `GLOBAL_REVIEW_CAP=1` (per-process `tracker.ts:getActiveReviews()` count) → `setPendingReview` + `GLOBAL_REVIEW_CAP_HIT`, then `checkLaunchGuard` → `canLaunchReview` (`inCriticalReview` + `findActiveReviewForQuest` + `final_acceptance` cap) → `setPendingReview` + `CRITICAL_REVIEW_SUPPRESSED_DUPLICATE`/`COALESCED`, then `registerActiveReview` + `awaitingReview`.
- `state.ts:sessionStates` is `Map<sessionId, StoredState>`; `getState(ctx)` returns per-session state. `tracker.ts` Maps are module-global per Node process. `inCriticalReview` flag is per-state.
- Because keys and Maps include `sessionId`, two `pi` processes (or two opencode sessions) on same `questId` get disjoint locks (`global:review:01a05dd6` vs `...01a05dd8`, `hadContention=false`). Intra-session serializes (`MUTEX_WAIT`), cross-session bypasses entirely. `GLOBAL_REVIEW_CAP` counts only current process.
- `pending_coalesce.ts:dequeuePendingIfNeeded` only runs from `policy/background.ts:onPending` (under hierarchical lock after review finishes) — no filesystem witness.

## Desired behavior

- Keep intra-session promise chain (fast, clear) and add a filesystem advisory lock per `questId` as the global witness: `current/<qid>/.review.lock` (`open` with `O_CREAT|O_EXCL`, `unlink` on finally, or `flock` if available). `runCriticalReview` acquires `questLockChains` **and** `.review.lock` before the `GLOBAL_REVIEW_CAP`/`canLaunchReview` checks. Duration is only the synchronous check+register section (not the sub-agent run) to keep “without blocking if I approve before agents return.”
- Coalescing stays via `pendingReviews` Map: second concurrent request while lock held does `setPendingReview({kind, boundaryKey, planVersion, stateHash, saveCount})` + `PENDING_COALESCED_RESOLVED/DROPPED` logs. When lock released, `onPending` dequeues atomically under lock (already `policy.ts:352 withQuestLock(global,quest)`).
- Replace `await import` swallow with static `import {logEvent} from "../../logging.ts"` leaf — `#23` — so `MUTEX_WAIT hadContention` and `waitMs`/`holdMs` are always counted via `reducers:138`.
- DAG stays `mutex → logging` leaf within allowlisted `messaging↔persistence` cycle.

## Manual validation in `pi` (step 2)

1. Open two `pi` sessions (or two terminals) with same project and `PI_QUEST_DAG` allowed.
2. In session A, trigger a `plan_review` (refinement while draft, or `quest_update_state` with material boundary change). Observe `MUTEX_ACQUIRED holdMs` and `awaitingReview` status.
3. While A’s review is `starting|running` (status bar `⏸ Awaiting plan_review`), in session B trigger another review for same `questId`. Before fix: B starts a second reviewer (two `starting` entries, `researchRound` bump). After fix: B gets `GLOBAL_REVIEW_CAP_HIT activeCount=1 cap=1` + `CRITICAL_REVIEW_COALESCED` and a single `PENDING_COALESCED_RESOLVED` queued; only one `activeReviews` entry; `grep MUTEX_WAIT` shows `hadContention:true waitMs>0`.
4. After A finishes (APPROVE/REVISE), B’s pending dequeues and runs once — no flood of `NO_PROGRESS`.

Related: #16, #21, #23, #29, #30, #33. Requires #36 first so draft gate is already enforced.
