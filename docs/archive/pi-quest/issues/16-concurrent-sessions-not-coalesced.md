---
id: 16
title: "Concurrent sessions on the same quest are not coalesced"
state: done
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

## Re-open evidence — `1788349108` (2026-09-02 11:38–11:45)

Same pattern reappears: 2 extra sessions (`01a061ed-9f79-71e3-a43d-a0cfb2cae5ff`, `01a061ed-9daf-77cf-bf96-27e63487c9f5`) `QUEST_REUSED` the same questId (`execution.log:231,238`, `SAVE_VERIFIED gen1 hash=a515d2be :229,236`, `INITIAL_PROMPT turn0 :232,239`) while `01a061e9-…` was in `REASSESSMENT_PENDING`.

Their first reads targeted the **extension's own source** (`.pi/extensions/pi-quest/src/critical_agent/{tracker.ts, policy/launch_guard.ts, policy/background.ts, prompt/build.ts}`, `src/hooks/index.ts`) instead of the site. They failed with `ENOENT …/policy/launch_guard.ts` (×2, thought `1ff1457050d0`), `Path not found …/critical_agent/policy`, `[fd error] …/pi-quest/src is not a directory`, `not found no realpath`, then looped:

- 5× `NO_PROGRESS` (`:411 turns=5`, `:421 turns=6`, `:434 turns=6`, `:460 turns=7`, `:480 turns=7`)
- 5× `REVIEW_DEDUP_HIT … reviewKind=direction reason=not_registered shard=direction` (`:412,422,435,461,481`)
- 5× `CRITICAL_REVIEW_SUPPRESSED_DUPLICATE … reviewId=not_registered reviewKind=direction` (`:413,423,436,462,482`)

Key nuance vs this issue's original fix: the dedup machinery fires, but with `reason=not_registered` / `reviewId=not_registered` — these reviews were **never registered**, so suppression is correctly refusing an out-of-scope/misrouted session that keeps re-launching, not coalescing a legitimate second reviewer. The underlying bug is the stray `QUEST_REUSED` mount (see updated #37 and new #56) and the wrong working-dir (new #58).
