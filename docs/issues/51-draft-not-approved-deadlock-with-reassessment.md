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
