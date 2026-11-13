---
id: 15
title: "STEP_SUMMARY is not emitted on quest_update_state boundary flips"
state: done
severity: low
requires: []
validates: "STEP_SUMMARY on boundary flip when semanticSummaryEnabled"
area: "tools/update/executor.ts:315-386, hooks/handlers.ts:169-213, gates.ts:157 getLifecycleState"
parent: 44
---
# Issue: `STEP_SUMMARY` is not emitted on `quest_update_state` boundary flips

- **Area:** `pi-quest` gates/tools — `tools/update/executor.ts:315-386`, `hooks/handlers.ts:169-213`, `gates.ts:157 getLifecycleState`, `config.ts: isSemanticSummaryEnabled`, `state.ts`, `utils/cache.ts:70`
- **Runs observed:** `1788280759` (enabled path not exercised; `SEMANTIC_SNAPSHOT` on `preBoundaryKey !== postBoundaryKey` at `371-386` but no `STEP_SUMMARY`); also `1788299416` 21× `STEP_SUMMARY research planVersion1 gate=RESEARCH_PENDING` repeats same key `reassessment:1:REASSESSMENT_PENDING` without delta
- **Severity:** Low — observability gap for reviewers; now mitigated by always-on `SEMANTIC_SNAPSHOT`+`STEP_SUMMARY` (code fixed) but still missing boundary-flip `STEP_SUMMARY` in `executor.ts:371`

Intended behavior per `EXPANDED:177,221` is: when `config.ts:isSemanticSummaryEnabled(state)` (flag `state>env PI_QUEST_SEMANTIC_SUMMARY>.pi/settings.json`, default `false`, leaf via `utils/cache.ts:70 getCachedSettingsJson` + `process.env` + `constants.ts`) is `true`, emit `STEP_SUMMARY` ≤1/turn at `handlers.ts:205-213` on `TURN_END` (covered) and once per `executor.ts:315` only on `boundaryKey`/`researchComplete` flip, payload `intent∈{research,plan-draft,awaiting-review,revising,implementing,verifying,reassessing}` via `getLifecycleState`, `planVersion, promptsCount, draftPromptsCount, activeGate`, `≤120` chars. Actual `executor.ts:371-386` emits only `SEMANTIC_SNAPSHOT` on `preBoundaryKey !== postBoundaryKey`; no `STEP_SUMMARY` even when `isSemanticSummaryEnabled===true`. The `STEP_SUMMARY` path is therefore only exercised via `handlers.ts` turn path, not via `quest_update_state`.

Related: #13, #14.
