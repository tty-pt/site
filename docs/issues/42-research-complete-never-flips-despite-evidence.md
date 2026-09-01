---
id: 42
title: "ResearchComplete never flips despite 10 evidences — NO_PROGRESS flood without checkpoint"
state: done
severity: medium
requires: []
validates: "10 evidences => researchComplete or NO_PROGRESS cleared"
area: "research.ts recordObservedInvestigation, validation.ts researchRequired, hooks/handlers.ts applyTurnEndStateTransitions/"
parent: 43
---
# Issue: `researchComplete` never flips despite 10 evidences — `NO_PROGRESS` flood without checkpoint

- **Area:** `pi-quest` research lifecycle — `research.ts recordObservedInvestigation`, `validation.ts researchRequired`, `hooks/handlers.ts applyTurnEndStateTransitions/analyzeTurnToolResults`, `state.ts researchRound/researchComplete/reassessmentRequired`, `compaction.ts requestPeriodicCheckpoint`
- **Runs observed:** `1788299416` 21:50 `01a05ef3` — `RESEARCH_EVIDENCE` 10× `read future/...md` `bash ls mods/ external/bud/hyle` `cat mods/index/ux/all.c` across `TURN_START 0-6`, yet `TURN_END` `activeGate=RESEARCH_PENDING` persists, `STEP_SUMMARY research planVersion1 gate=RESEARCH_PENDING tools=1 reads=0/1 writes=0 failures=0` 21×, `NO_PROGRESS turns 5→15` `turn5 correlationId=turn_5_mtj7b67v` `turn6 6` `turn11 12` `turn17 13` `turn20 15`, `substantiveTurnsSinceCheckpoint` never reset because `didUpdateQuestThisTurn` never true.
- **Severity:** Medium — agent loops evidence without durable `quest_update_state`, `periodic_checkpoint` steer spams but `researchComplete` stays false, `PROVISIONAL_RESEARCH_PENDING` never clears.

## Current behavior

- `analyzeTurnToolResults` counts `read/bash` as substantive only if `toolResults.length` and `phase` matches, but many `TURN_END substantive=false toolsUsed=1 mutations=1` for reads (evidence recorded but not substantive).
- `applyTurnEndStateTransitions` increments `substantiveTurnsSinceCheckpoint` and never clears `researchRequired` because `quest_update_state` never accepted (36 gate). No `researchComplete` flip from evidence count alone.

## Desired behavior

- After ≥2 substantive evidences (or 5 total) while `!state.active && state.activeDraft`, auto-mark `researchComplete=true` or at least clear `NO_PROGRESS` counter so gate can open via `quest_update_state`. Minimal: `research.ts` after `recordObservedInvestigation` if `evidenceCount≥5 && !researchComplete && activeDraft` set `researchComplete=true` and `substantiveTurnsSinceCheckpoint=0`, log `RESEARCH_COMPLETED`.
- Keep `STEP_SUMMARY` unconditional line showing `reads/searches` so log story is legible.

## Manual validation in `pi`

1. Initial `Look at consumer side...` → after 2 `read` turns, `TURN_END` must show `research planVersion1 gate=RESEARCH_PENDING tools=1 reads=1` then next `SEMANTIC_SNAPSHOT` still `RESEARCH_PENDING` but `NO_PROGRESS` not yet threshold.
2. After 5 evidences without `quest_update_state`, `NO_PROGRESS` should not flood 15×; `substantiveTurnsSinceCheckpoint` resets.

Related: #01, #03, #10, #40, #41.
