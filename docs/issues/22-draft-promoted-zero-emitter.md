---
id: 22
title: "DRAFT_PROMOTED type exists, 0 emitter — 23/24 types emitted"
state: ready
severity: low
requires: []
blocked_by: []
validates: "grep DRAFT_PROMOTED 3 emitters"
area: "22-draft-promoted-zero-emitter.md"
---
# Issue: `DRAFT_PROMOTED` type exists, 0 emitter — 23/24 types emitted

- **Area:** `pi-quest` logging/lifecycle — `logging/types.ts:148`, `logging/summary/reducers.ts:127,161`, `commands/promote.ts:58-66`, `lifecycle.ts:99-110`, `hooks/index.ts:261`
- **Runs observed:** HEAD 2026-09-01 (`grep -rn 'logEvent.*DRAFT_PROMOTED' src | wc -l` 0; type+reducer 3 hits only)
- **Severity:** Low — last of 24 B2/B3 types missing; `rg DRAFT_PROMOTED` in `execution.log` empty

`QuestLogEventType` has 24 members (`DRAFT_APPENDED`…`STEP_SUMMARY` `types.ts:121-169` B2 20 + B3 4). Precise emitter counts at HEAD: `DRAFT_APPENDED 1`, `DRAFT_APPEND_DEDUPED 1`, `DRAFT_CONVERSATIONAL_IGNORED 1` (bug #19), `DRAFT_PROMOTED 0`, `DRAFT_DISCARDED 4`, `SYNTHETIC_FILTERED 4`, `CLASSIFICATION_RESULT 9`, `REQUIRE_CONFIRM 2`, `ATTEMPT_INCREMENTED 1`, `PENDING_COALESCED_DROPPED/RESOLVED 1/1`, `PLAN_REVIEW_SUPPRESSED/FIRST_PLAN_REVIEW_ALREADY_FIRED`, `REVIEW_DEDUP_HIT 4`, `MUTEX_* 2`, `CRITICAL_REVIEW_ORPHAN_CLEARED 3`, `SNAPSHOT_FALLBACK 2` (see #18), etc. — **23/24 emitted, `DRAFT_PROMOTED` the 1** (`REMAINING_WORK.md §2.9:171`, Appendix A §8, Bunch 2).

All 3 promotion call sites do `writeFile`+`copyFile(future-archive)`+`unlink/rename` without logging:

- `hooks/index.ts:261` `promoteDraft` path
- `commands/promote.ts:58` `writeFile(destPath,content)` + `64 logEvent DRAFT_DISCARDED` (wrong type)
- `lifecycle.ts:99-110` `copyFileSync` before `rename` + `108 DRAFT_DISCARDED`

Fix (`§2.9:174`): after each `writeFile(destPath,content)` add `try{ const h=createHash("sha256").update(content).digest("hex").slice(0,12); logEvent("DRAFT_PROMOTED" as any, `draft promoted`, {quest:slug, slug, hash:h, questId:qId} as any)}catch{}` reuse `createHash` already `hooks/index.ts:16`, add `node:crypto` import to `promote.ts`/`lifecycle.ts`. Leaf, +0 DAG.

Verification: `rg "DRAFT_PROMOTED" .pi/extensions/pi-quest/src --include="*.ts" -n` → 3 emitters (one per path) + `rg DRAFT_PROMOTED .pi/quest/current/*/execution.log` on `quest` promotion.

Related: #04, #18, #24.
