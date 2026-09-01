---
id: 28
title: "as any regression 213 hits — reassess from scratch, target 0"
state: ready
severity: low
requires: [27]
validates: "grep as any 0 (allow state.ts proxy)"
area: "grep -rn 'as any' src --include='*.ts' | wc -l, as unknown, unknown bare"
parent: 44
---
# Issue: `as any` regression 213 hits — reassess from scratch, target 0

- **Area:** `pi-quest` quality — `grep -rn "as any" src --include="*.ts" | wc -l` 213, `as unknown` 1, `unknown bare` ~37, `deno lint 895` (`565 no-explicit-any +148 no-unused-vars +121 no-empty`)
- **Runs observed:** HEAD 2026-09-01 full scan (not doc `11`); `deno check --node-modules-dir=auto` 0, else 1 `npm:@types/node`
- **Severity:** Low — type-safety gate not closed; `PLAN_REMOVE_AS_ANY.md:3 373 hits` stale, `src:11` false

Per-file breakdown at HEAD (`REMAINING_WORK.md §2.4:131` + Appendix C′ corrected):

`reconstruction.ts 27 not 15`, `hooks/index.ts 26`, `state.ts 15 not 2` (+13), `diagnostic/* ~20` (`diagnostic/packaging 9` missed), `handlers.ts 16`, `executor.ts 18 not 14` (+4), `policy.ts 18 not 16` (+2), `commands/* 8` (`promote 8` missed), `classification.ts 6`, `messaging.ts 4`, `paths.ts 2`, `mutex.ts 2`, plus `lifecycle.ts:108,220` `ctxRes as any`, `persistence.ts:86`, `compaction/*`, remainder `types.ts:186` `any`.

Doc Phase 1 inventory `hooks/index.ts` `targetState.awaitingReview?.reviewId` etc. already changed, counts drift. New target: undetailed single stage from scratch after P0-P1 green, not per-file table now.

- Goal `src:0 as any` (allow only `state.ts` proxy `get`/`set` + `globalThis` narrow boundary `utils/cache.ts:SettingsJson` where `unknown` narrowed), `unknown` allowed only after `isStoredState` guard `reconstruction.ts` (see #27).
- Strategy: `deno check --allow-all` must stay green + `prettier --check ".pi/extensions/pi-quest/src/**/*.ts"` single pass (no stale per-file table); `379 lines >120` remain, `prettier` trailing spaces 0 but line-length still.
- Order: trivial deletions where `StoredState` typed `types.ts` + `logging/summary/state.ts` → redundant `as unknown` → precise `Record<string,unknown> types.ts:details` + `ArchiveContext|{error} lifecycle.ts` + `isStoredState` guard — but **do not enumerate per-file now**; fresh `grep` on execution determines.

Full audit preserved without line numbers: `as any` 213, `as unknown` 1 (not 38), `unknown bare` ~37; per-file corrected aggregates above show doc low by 13/12/4/2 and omits 9 `diagnostic/packaging` + 8 `commands/promote`; `deno lint 895` (`565 no-explicit-any`, `148 no-unused-vars`, `121 no-empty`, plus `24 no-process-global`, `23 require-await`, `11 prefer-const` missed) not `1569/312/131`; `deno check --node-modules-dir=auto` 0 else 1 `npm:@types/node`; Appendix C verdict: no quality gate closes — only `deno check` and trailing spaces 0 are truly DONE.

Verification: `grep -rn "as any" .pi/extensions/pi-quest/src | wc -l` `213→0` + `deno check --node-modules-dir=auto` 0 + `prettier --check` 0 + `deno test --allow-all` `55→60 passed` after quality reassess.

Related: #27, #29, #17, #18, #33.
