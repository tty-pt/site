---
id: 21
title: "10 silent return null without REVIEW_DEDUP_HIT / CRITICAL_REVIEW_SUPPRESSED"
state: done
severity: medium
requires: []
validates: "grep return null 0 silent (each has REVIEW_DEDUP_HIT etc)"
area: "critical_agent/policy.ts:405,440,441,444,511,512,515,548, critical_agent/policy/pending_coalesce.ts:32,64, logging/types"
parent: 44
---
# Issue: 10 silent `return null` without `REVIEW_DEDUP_HIT` / `CRITICAL_REVIEW_SUPPRESSED`

- **Area:** `pi-quest` critical-agent policy — `critical_agent/policy.ts:405,440,441,444,511,512,515,548`, `critical_agent/policy/pending_coalesce.ts:32,64`, `logging/types.ts`, `logging/summary/reducers.ts:137`
- **Runs observed:** HEAD 2026-09-01 (`grep -rn "return null" src/critical_agent --include="*.ts" | wc -l` 10 silent of 17 total)
- **Severity:** Medium — `grep current/*/execution.log` cannot distinguish intentional dedup from bug

Adversarial re-audit (`REMAINING_WORK.md §2.8:163`, Appendix A §3) found 10 silent early-returns that precede no `logEvent`:

| Location | Code | Preceding log |
|---|---|---|
| `pending_coalesce.ts:32` | `if(pendings.length===0) return null` | none |
| `pending_coalesce.ts:64` | `if(candidates.length===0) return null` | only `for(s of stale)clear` |
| `policy.ts:405` | `if(!registered) return null` draft branch | none |
| `policy.ts:440` | `if(!s.active||!isRootQuest) return null` | none |
| `policy.ts:441` | `if(s.reassessmentRequired) return null` | none |
| `policy.ts:444` | `if(!registered) return null` root check | none |
| `policy.ts:511` | `if(!s.active||!isRootQuest) return null` direction | none |
| `policy.ts:512` | `if(s.reassessmentRequired||researchRequired||awaitingUserConfirmation) return null` | none |
| `policy.ts:515` | `if(!registered) return null` | none |
| `policy.ts:548` | `if(getPendingReview) return null` | none |

Done paths (not silent) — full 17-row breakdown (7 DONE, 10 silent) from Appendix A §3:

- Done: `policy.ts` draft valid `FIRST_PLAN_REVIEW_ALREADY_FIRED` + `REVIEW_DEDUP_HIT` dedup; root `FIRST_PLAN_REVIEW_ALREADY_FIRED` + `REVIEW_DEDUP_HIT` + `CRITICAL_REVIEW_SUPPRESSED_DUPLICATE` chain; throttled `DIRECTION_REVIEW_THROTTLED`; cooldown `DIRECTION_REVIEW_THROTTLED`; direction dedup `REVIEW_DEDUP_HIT` (2) + `CRITICAL_REVIEW_SUPPRESSED_DUPLICATE`; `pending_coalesce.ts` after `PENDING_COALESCED_DROPPED` (when `firstPlanReviewFired`) — 7 DONE rows.
- The 10 silent rows above are `STILL MISSING` (stale doc cited `policy.ts:295,403,438,442` shifted ~2-3 at HEAD).

Fix (line-free, audit-preserved): before each silent `return null` add `try{logEvent("REVIEW_DEDUP_HIT", `dedup hit`, {shard:"draft"|"root"|"direction", reason:"not_registered"|"not_root"|"reassessmentRequired"|"researchRequired"|"awaitingUserConfirmation", quest: s.active || ""})}catch{}` or `logCriticalReviewTransition("CRITICAL_REVIEW_SUPPRESSED_DUPLICATE", ...)` for policy, and for `pending_coalesce.ts:32,64` add `try{logEvent("PENDING_COALESCED_DROPPED", ..., {staleCount, candidateCount})}catch{}` when `candidates===0` after stale clear (distinct from already-present `pending_coalesce.ts:35` `PENDING_COALESCED_DROPPED` when `firstPlanReviewFired`).

Leaf `logging/types.ts`, +0 DAG, allowlisted `messaging↔persistence` unchanged. Header miscount (9 vs 10 silent — misses `441,444,512`) corrected here.

Tests: `logging_maturity.test.ts:P3b` `checkAndTriggerPlanReview` when `!registered` → `c.includes("REVIEW_DEDUP_HIT")` / `CRITICAL_REVIEW_SUPPRESSED`.

Verification: `grep -rn "return null" src/critical_agent --include="*.ts" | wc -l` 17 total (10 silent) → after fix 0 silent (each preceded by `REVIEW_DEDUP_HIT`/`CRITICAL_REVIEW_SUPPRESSED`/`PENDING_COALESCED_DROPPED`); `rg -n "return null" src/critical_agent/policy.ts` 7 hits; `rg PENDING_COALESCED_DROPPED src/critical_agent/policy/pending_coalesce.ts`.

Related: #13, #16, #22, #33.
