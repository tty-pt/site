---
id: 50
title: "Snapshot fallback draft_boundary_fallback fires spuriously when future file slug mismatched"
state: ready
severity: medium
requires: [47, 49]
validates: "grep SNAPSHOT_FALLBACK execution.log must be 0 during drafting unless git diff fails"
area: "critical_agent/snapshot.ts:51-69 + execution.log:21"
parent: 43
---
# Issue: Snapshot fallback draft_boundary_fallback fires spuriously when future file slug mismatched

- **Area:** `pi-quest` snapshot — `critical_agent/snapshot.ts:51-69` (try `readFile FUTURE_DIR/slug.md` then `logEvent SNAPSHOT_FALLBACK reason=draft_boundary_fallback`) + `execution.log:21` `1788305314`
- **Runs observed:** `1788305314` `21 SNAPSHOT_FALLBACK reason=draft_boundary_fallback reviewId=rev_mtjatemx_eir8` at `turn0` immediately after draft create; fallback uses `state.lastPlanReviewBoundaryKey` stale due to #49/#52 ENOENT.
- **Severity:** Medium — spurious fallback uses stale boundary, fuels `draft_not_approved` loop.

Related: #18 (doc staleness), #47, #49.
