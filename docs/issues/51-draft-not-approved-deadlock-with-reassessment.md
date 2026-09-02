---
id: 51
title: "draft_not_approved blocks quest_update_state while REASSESSMENT_PENDING requires it — deadlock"
state: done
severity: high
requires: [46, 50]
validates: "quest_update_state during REASSESSMENT_PENDING+draft_not_approved must be GATE_BLOCKED not TOOL_FAILURE"
area: "tools/update/executor.ts:syncQuest draft_not_approved + critical_agent/policy.ts:452-501 + execution.log:110-113"
parent: 43
---
# Issue: draft_not_approved blocks quest_update_state while REASSESSMENT_PENDING requires it — deadlock

- **Area:** `pi-quest` gates — `tools/update/executor.ts:syncQuest draft_not_approved` + `critical_agent/policy.ts:452-501 checkAndTriggerPlanReview` + `execution.log:110-113` `1788305314`
- **Runs observed:** `1788305314` `110 REVIEW_DEDUP_HIT reason=draft_not_approved boundaryKey=draft:look-consumer…:6c01c6f341f9` → `113 TOOL_ACTIVITY quest_update_state failure reason=draft_not_approved` at `turn9` while `activeGate=REASSESSMENT_PENDING` (`81-86`). Agent stuck: cannot `quest_update_state` (blocked) but reassessment gate says implement not allowed. Loop `turn6-10 revising`.
- **Severity:** High — valid path is await coalesced review, not retry `quest_update_state`.

Related: #36 (bypasses approval done), #38 (invalidate pending done), #39 (per-boundary ready), #46.

## Re-open evidence — `1788349108` (2026-09-02 11:38–11:45) — broader reassessment deadlock

This run reproduces the deadlock **more severely and via a different contract path** than the original `draft_not_approved` case.

Plan-review returned `UNCERTAIN` at turn 19 (`:203 PLAN_REVIEW_UNCERTAIN verdict=UNCERTAIN reviewId=rev_mtk116zx_lmi1`, `:204 REMEDIATION_REQUIRED`), then the main session `01a061e9-…` was stuck in `REASSESSMENT_PENDING` from turn 23 through turn 41 (~19 turns, log ends at `:717` still `activeGate=REASSESSMENT_PENDING`, no resolution). The agent attempted `reassessmentComplete` 5× and was rejected each time with a **different** contract failure (4 distinct):

- `:379/382` (turn 23) `REASSESSMENT_REQUIRED` + `REASSESSMENT_REJECTED` — "A non-empty `reassessmentConclusion` is required"
- `:490/493` (turn 25) — "Plan confidence is 'low'. To complete research with low confidence, you must pass `allowLowConfidence: true` AND provide explicit justification in `planConfidenceReason`" (+ concurrent `:495 SAVE_VERIFICATION_FAILURE`)
- `:565/568` (turn 29), `:629/632` (turn 34), `:667/670` (turn 37) — **"Investigation receipt was for initial research, but fresh investigation is required after the reassessment trigger"** (×3 identical). The agent kept passing the initial-research receipt instead of performing a fresh post-trigger investigation.

Additional gating traps that contributed:
- `:442/444` (turn 24) `GATE_BLOCKED` + `IMPLEMENTATION_BLOCKED tool=edit` + `:446 blocked … Tool 'edit' execution blocked in state REASSESSMENT_PENDING`
- `:703/704/706/708` (turn 40) `UNKNOWN_TOOL tool=bash` + `GATE_BLOCKED` + `IMPLEMENTATION_BLOCKED tool=bash code=UNKNOWN_TOOL_BLOCKED` — `bash` was routed as unknown and blocked in the `revising` gate.

The original fix (GATE_BLOCKED not TOOL_FAILURE) is present, but the agent still has no valid forward path: the rejection messages do not steer it to the exact missing field, and `bash` (its investigation tool) is disallowed in this gate. See new #57 for the dedicated follow-up on the opaque reassessment contract, and #12 for the Save-Verification false positives that compounded the loop.
