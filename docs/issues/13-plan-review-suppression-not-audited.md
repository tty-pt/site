---
id: 13
title: "Plan-review suppression is silent — PLAN_REVIEW_SUPPRESSED_MATERIAL_CHANGE never emitted"
state: ready
severity: low
requires: []
validates: "PLAN_REVIEW_SUPPRESSED_MATERIAL_CHANGE logged when material change suppressed"
area: "tools/update/executor.ts:223-250 maybeTriggerPlanReview, logging/types.ts:156, logging/summary/reducers.ts:162"
parent: 44
---
# Issue: Plan-review suppression is silent — `PLAN_REVIEW_SUPPRESSED_MATERIAL_CHANGE` never emitted

- **Area:** `pi-quest` plan review — `tools/update/executor.ts:223-250 maybeTriggerPlanReview`, `logging/types.ts:156`, `logging/summary/reducers.ts:162`
- **Runs observed:** `1788280759` (`grep PLAN_REVIEW_SUPPRESSED` empty, despite `firstPlanReviewFired && isMaterialPlanChange`)
- **Severity:** Low — audit gap, hidden vs suppressed ambiguity

`logging/types.ts:156` declares `PLAN_REVIEW_SUPPRESSED_MATERIAL_CHANGE` and `FIRST_PLAN_REVIEW_ALREADY_FIRED`, and `reducers.ts:162 handlePendingEntry` routes them, but `maybeTriggerPlanReview:223` computes `isMaterialPlanChange = preBoundaryKey !== postBoundaryKey`, `firstPlanReviewFired = !!targetState.firstPlanReviewFired || !!lastPlanReviewApproval`, `alreadyRequested/ApprovedForBoundary`, and `shouldTriggerReview = !alreadyApproved && !alreadyRequested && (isInitialResearchCompletion||isFirstPlanDraft||isMaterialPlanChange||isRevisionAfterRejection)`. When `!shouldTriggerReview` it returns `""` with zero log. Intended behavior per `HIGH_LEVEL_PLAN_CONFIGURABLE_REVIEWER.md §11` and `EXPANDED §3.1 L1#10` is to emit `PLAN_REVIEW_SUPPRESSED_MATERIAL_CHANGE {preBoundaryKey slice(0,8), postBoundaryKey slice(0,8), boundaryKey, planVersion}` when `firstPlanReviewFired && isMaterialPlanChange`, and `FIRST_PLAN_REVIEW_ALREADY_FIRED {shard: root, boundaryKey slice(0,8)}` when `alreadyRequested/Approved`.

Related: #14, #16.
