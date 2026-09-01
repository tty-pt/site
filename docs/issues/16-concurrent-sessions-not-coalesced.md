---
id: 16
title: "Concurrent sessions on the same quest are not coalesced"
state: ready
severity: high
requires: []
validates: "single flight: 2 sessions same quest => 1 active + coalesced pending"
area: "critical_agent/policy/launch_guard.ts, critical_agent/policy/pending_coalesce.ts, critical_agent/tracker.ts"
parent: 44
---
# Issue: Concurrent sessions on the same quest are not coalesced

- **Area:** `pi-quest` critical agent — `critical_agent/policy/launch_guard.ts`, `critical_agent/policy/pending_coalesce.ts`, `critical_agent/tracker.ts`, `critical_agent/runner.ts`, `state.ts:6,12 lastGeneratedSec`
- **Runs observed:** `1788280759` at `134-143,161-172,241-253,389-399` four sessions `01a05dd6`, `01a05dd8-c2e6`, `01a05dd8-c3a5`, `01a05dd9-*` interleaved
- **Severity:** High — amplifies reassessment and `NO_PROGRESS` feedback loop

From `16:41:23.570Z` onward three additional `01a05dd8/01a05dd9` sessions each log `SAVE_STARTED SAVE_VERIFIED gen1`, `QUEST_REUSED`, `INITIAL_PROMPT turn0`, `TURN_START reassessment T0` for the same `questId 1788280759` while `01a05dd6` is already `REASSESSMENT_PENDING round4`. Each session independently increments `researchRound 4→5→6`, records `TOOL_FAILURE`, triggers `REASSESSMENT_REQUIRED`, and emits `NO_PROGRESS` and `periodic_checkpoint` steers (`300,313,321,338,455,464`). `launch_guard.ts: canLaunchReview` / `tracker.ts: inCriticalReview` and `pending_coalesce.ts` do not enforce `maxConcurrency=1` per `questId` across sessions, so the same reassessment is investigated in parallel and the `NO_PROGRESS turns=5-8` threshold (`hooks/index.ts` / `research.ts`) floods the log (20+ emissions in this run). `state.ts:12 lastGeneratedSec` also advances per session, contributing to `generateQuestId` monotonic bumps.

Related: #10, #11, #13.
