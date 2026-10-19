---
id: 30
title: "Epics A/B/C/D remaining per CONFIGURABLE_REVIEWER — 1-line refs not filed"
state: deferred
severity: low
requires: [33]
validates: "archive/CONFIGURABLE_REVIEWER 1-line refs present"
area: "config.ts, gates.ts, policy.ts"
parent: 45
---
# Issue: Epics A/B/C/D remaining per CONFIGURABLE_REVIEWER — 1-line refs not filed

- **Area:** `pi-quest` epics — `config.ts`, `gates.ts`, `policy.ts`, `critical_agent/*`, `hooks/index.ts`, `compaction/*`, `diagnostic/*`
- **Runs observed:** `1788280759` bundle `Verification: PASSED` but B3.1 partial, C/D deferred
- **Severity:** Low — roadmap / 1-line references, not blocking §2 logging fixes

`REMAINING_WORK.md §2.6` aggregates four epics deferred as 1-line `archive/CONFIGURABLE_REVIEWER.md` refs. No dedicated issue existed — only `README` deferred pointer. All file references below are module-level (no line numbers).

- **A Configurable reviewer** — `isReviewerEnabled` / `isReviewerRequireHumanConfirm` / `isSemanticSummaryEnabled` / `isThoughtLoggingEnabled` in `config.ts` leaf (`constants.ts`, `state.ts`), `gates.ts:isPlanReviewValidForState`, `policy.ts:runCriticalReview` top guard. Flags default `false` via `constants.ts` + `utils/cache.ts:getCachedSettingsJson` + `env`. B2 pre-instruments now `rg`-checkable.
- **B Single-shot** — `state.ts:firstPlanReviewFired` singleton, `tools/update/executor.ts` + `policy.ts` + `critical_agent/policy/launch_guard.ts` per-quest `attemptKey`, `critical_agent/policy/pending_coalesce.ts` drop, `rejected.ts` reset on `REVISE`/`UNCERTAIN`. Prevents duplicate plan-review.
- **C Flawless** — `P0 prompt/build.ts` decontam, `P1` hierarchical lock `policy.ts` + `utils/mutex.ts:GLOBAL_REVIEW_CAP=1`, `P2 checks.ts` verb-filter, `P3` synthetic `constants.ts` 48→14 `SYNTHETIC_PROMPT_PREFIXES` `messaging.ts:STARTS_WITH` vs `CONTAINS`, `P4` orphan `hooks/index.ts` + `compaction/resume.ts` fallback with `pendingResume`.
- **D Simplification** — `draft vs root unify` `policy.ts:questId`, `tracker.ts:per-hash→per-quest`, `mutex single global` simplification. Must not re-prune `constants.ts` (already pruned C P3) — verify only.

Status per `REMAINING_WORK.md §2.6`: A/B/C/D deferred, B3 `DONE` per audit, B3.1 `INITIAL_PROMPT` partial, `rg`-checkable via P0-P1 green, not blocking §2.1-2.14. Original source `HIGH_LEVEL_PLAN_CONFIGURABLE_REVIEWER.md` truncated 210 lines (orig 743) moved to `docs/archive/` after confirmation.

Related: #09, #13, #16, #19, #21, #23. See also #31 obligations (P2 integration after gap-fill green).
