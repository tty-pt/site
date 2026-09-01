---
id: 34
title: "Rollout order for remaining fixes not sequenced"
state: deferred
severity: low
requires: [33]
validates: "docs/FIX_ORDER.md stages 1-5 present"
area: "§2.1-2.14, §4, scripts/check-pi-quest-dag.ts"
parent: 45
---
# Issue: Rollout order for remaining fixes not sequenced

- **Area:** `pi-quest` rollout — all `§2.1-2.14` plus `§4` verification, `scripts/check-pi-quest-dag.ts`, `deno test`, `npm run zip`
- **Runs observed:** `1788280759` → HEAD `54 passed 302 steps → 55 → 60 → 62`
- **Severity:** Low — execution dependency, not functional gap

`REMAINING_WORK.md §5` stages remaining work to keep `deno check` green and DAG `passed` at each step. No per-issue file stated order — would be lost on deletion.

Ordered execution:

1. **§2.1-2.3 + §2.8-2.9** — `snapshot.ts:SNAPSHOT_FALLBACK` (2 emitters already landed, keep 1 if `captureSnapshot` null reappears) + `hooks/index.ts` conversational bug + `resolve.ts` manifest wiring + `policy.ts` 10 silent returns + `DRAFT_PROMOTED` 3 emitters → `DAG passed` + `logging_maturity.test.ts` green → `55→57 passed`.
2. **§2.10-2.13** — `mutex.ts:await import→static`, `commands/quest.ts:5th bare rename` archive-before-rename, `resolve.ts:futureCount` + `future-archive` + `e.isFile()` fix, `reconstruction.ts:orphan readdirSync` + `hasDraft` disk fallback → `DAG passed` + `reconstruction_draft.test.ts` orphan → `58 passed`.
3. **§2.4-2.5 + §2.14 undetailed quality reassess** — `as any 213→0` + `prettier --check` + `tryLog` helper (`logging/safe.ts`) + `isStoredState` guard + `ArchiveContext|{error}` union — single stage from scratch, not per-file table (fresh `grep` on execution determines), `deno check --node-modules-dir=auto` + `grep catch 121→<10` → `62 passed 335 steps`.
4. **Deferred** — Epics A/B/C/D + obligations P2 per `archive/CONFIGURABLE_REVIEWER.md` + `CONCEPT_OBLIGATIONS_LIFECYCLE.md` — now `rg`-checkable via P0-P1 green, not blocking §2.

Follow-on mapping:

| Remaining (§2) | Original doc source | Next epics (deferred, archive refs) |
|---|---|---|
| §2.1-2.3 | `GAPFILL_DETAILED` 7 gaps + `LOGGING_FIRST` L0 | Epics A/B single-shot + C P4 orphan |
| §2.4-2.5 | `PLAN_REMOVE_AS_ANY.md` reassessed + try-catch bare | Epic D `questId` unify + `SYNTHETIC` prune |

Risks: `SNAPSHOT_FALLBACK` leaf `logging/types.ts` + `hooks` move keeps `hooks→logging` leaf, no new cycle; as-any after §2.1-2.3 keeps `deno check` green (snapshot has no `as any`); `tryLog` helper hides only `existsSync` guards where `packaging.ts` expected.

Related: #18-#29, #30, #31, #33.
