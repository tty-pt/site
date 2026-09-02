---
id: 57
title: "Reassessment completion contract is opaque — agent loops with stale investigation receipt and never escapes REASSESSMENT_PENDING"
state: ready
severity: high
requires: [51]
validates: "agent completes reassessment in ≤2 turns after fresh post-trigger investigation, without stale-receipt loop"
area: "tools/update/executor.ts, validation/consistency/audit.ts, hooks/index.ts, critical_agent/policy.ts"
parent: 43
---
# Issue: Reassessment completion contract is opaque

- **Area:** `pi-quest` reassessment — `tools/update/executor.ts` (`reassessmentComplete` handling), `validation/consistency/audit.ts` (save verification), `hooks/index.ts` / `critical_agent/policy.ts` (gate `REASSESSMENT_PENDING`), `execution.log` reassessment rejection messages.
- **Runs observed:** `1788349108` (2026-09-02 11:38–11:45) — plan-review `UNCERTAIN` at turn 19 (`:203 PLAN_REVIEW_UNCERTAIN verdict=UNCERTAIN reviewId=rev_mtk116zx_lmi1`, `:204 REMEDIATION_REQUIRED`), then the main session `01a061e9-…` was stuck in `REASSESSMENT_PENDING` for ~19 turns (turns 23–41, log ends at `:717` still `activeGate=REASSESSMENT_PENDING`). The agent attempted `reassessmentComplete` 5× and was rejected each time with 4 distinct contract failures:
  - `:379/382` (turn 23) `REASSESSMENT_REQUIRED` + `REASSESSMENT_REJECTED` — "A non-empty `reassessmentConclusion` is required stating what fresh investigation established about the contradiction"
  - `:490/493` (turn 25) — "Plan confidence is 'low'. To complete research with low confidence, you must pass `allowLowConfidence: true` AND provide explicit justification in `planConfidenceReason`" (plus `:495 SAVE_VERIFICATION_FAILURE`)
  - `:565/568` (turn 29), `:629/632` (turn 34), `:667/670` (turn 37) — **"Investigation receipt was for initial research, but fresh investigation is required after the reassessment trigger"** (×3 identical)
  Also: `:442/444 GATE_BLOCKED` + `IMPLEMENTATION_BLOCKED tool=edit` (turn 24) and `:703/704/706/708 UNKNOWN_TOOL tool=bash` + `GATE_BLOCKED` in `revising` gate (turn 40) — the agent's investigation tool was blocked while being told a fresh investigation was required (see #52).
- **Severity:** High — terminal deadlock; the agent has no valid forward path even though the gate is correctly `GATE_BLOCKED not TOOL_FAILURE` per #51.

## Current behavior

- `executor.ts`'s `quest_update_state({ reassessmentComplete: true, ... })` handler validates several fields at once (`reassessmentConclusion` non-empty, post-trigger `RESEARCH_EVIDENCE` receipt present, `allowLowConfidence` + `planConfidenceReason` when `planConfidence=low`, `Files Modified` save-verification). On failure it emits a single `reassessmentComplete refused — …` reason plus `SAVE_VERIFICATION_FAILURE` for unrelated `Files Modified` issues. The agent sees a new reason each turn and never assembles a valid submission.
- The "fresh investigation is required" check compares the last `RESEARCH_EVIDENCE` timestamp against the reassessment trigger epoch. The agent keeps handing in the initial-research receipt (28 reads before turn 19) instead of performing a fresh read after the trigger. The rejection message does not state **what counts as fresh** (a `read`/`code-search` after the trigger timestamp) or **which investigation receipt** it is looking for.
- Low-confidence requires both `allowLowConfidence:true` and `planConfidenceReason`, but the agent never learns this until turn 25, after first trying without them.

## Desired behavior

- Make the contract self-explanatory and steerable in a single rejection message: when `reassessmentComplete` is refused, emit **all** missing fields at once (or the single next required action) with an example payload, e.g. `reassessmentComplete refused — need { reassessmentConclusion: "<what fresh read established>", <fresh read after <triggerTs>>, if planConfidence=low then allowLowConfidence:true + planConfidenceReason, fix Files Modified per #12 }`.
- Allow `bash` (or at least `read`/`code-search`) in the `revising` / `REASSESSMENT_PENDING` gate so the agent can actually perform the required fresh investigation (see #52). Currently `bash` is `UNKNOWN_TOOL` in that gate (`:703/708`).
- Gate `Files Modified` save-verification per #12 so it does not block the same `quest_update_state` that carries `reassessmentComplete` with false positives.

## Manual validation in `pi`

1. Reproduce `1788349108`: save a plan (`planVersion=1`, `planConfidence=low`), trigger `plan_review → UNCERTAIN`, enter `REASSESSMENT_PENDING`.
2. Send a single `quest_update_state` with `reassessmentComplete:true` missing one field — the error message should list exactly what's missing and how to satisfy it, including the freshness requirement.
3. The agent should be able to complete reassessment in ≤2 turns after performing one fresh `read`/`code-search` and supplying the conclusion + low-confidence justification.

Related: #12, #51, #52, #56.
