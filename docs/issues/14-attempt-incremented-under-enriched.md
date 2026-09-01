---
id: 14
title: "ATTEMPT_INCREMENTED is under-enriched"
state: ready
severity: low
requires: []
validates: "ATTEMPT_INCREMENTED has boundaryKey/saveHash/saveCount"
area: "critical_agent/policy/launch_guard.ts:50-64, critical_agent/policy.ts, critical_agent/snapshot.ts:50"
parent: 44
---
# Issue: `ATTEMPT_INCREMENTED` is under-enriched

- **Area:** `pi-quest` critical agent policy — `critical_agent/policy/launch_guard.ts:50-64`, `critical_agent/policy.ts`, `critical_agent/snapshot.ts:50`
- **Runs observed:** `1788280759` at `110`, `214`, `365` `ATTEMPT_INCREMENTED {attemptKey,attempts,stateHash}` only
- **Severity:** Low — D-collapse diagnosis needs more fields

`launch_guard.ts:50 buildReviewBoundaryKey(slug,kind,planVersion,hash,saveCount)` increments `criticalReviewAttempts[attemptKey]++` and logs `ATTEMPT_INCREMENTED {quest,questId,sessionId,reviewId,reviewKind,attemptKey,attempts,planVersion,stateHash}`. Per `HIGH_LEVEL_PLAN_V2_GAPFILL_DETAILED §Gap2` and `EXPANDED:150` every increment should also carry `boundaryKey slice(0,8)`, `saveHash slice(0,8)`, `saveCount` (and `boundaryKey` full). Spec D collapse detection (`rg ATTEMPT_INCREMENTED attemptKey=slug:plan_review attempts=4 boundaryKey=v1:ab… saveHash=` ) therefore fails. The helper `createHash('sha256').update(boundaryKey).slice(0,12)` (`snapshot.ts:50`) is available but not used here; existing `currentHash` slice is sufficient.

Related: #13, #15.
