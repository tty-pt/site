---
id: 20
title: "Manifest filteredCount/opencodeSessionId/startMs/elapsedMaxMs null until log parsed"
state: done
severity: medium
requires: [25]
validates: "manifest.txt 9 fields even on cold start"
area: "diagnostic/hierarchy/resolve.ts:268-293, diagnostic/summary.ts:209-258, generateRunManifest"
parent: 45
---
# Issue: Manifest `filteredCount/opencodeSessionId/startMs/elapsedMaxMs` null until log parsed

- **Area:** `pi-quest` diagnostic — `diagnostic/hierarchy/resolve.ts:268-293`, `diagnostic/summary.ts:209-258` `generateRunManifest`, `diagnostic/packaging.ts:31,297`
- **Runs observed:** `1788280759` (`manifest.txt filteredCount` stuck 0), HEAD 2026-09-01cold start `logExists===false`
- **Severity:** Medium — bundle `Verification: PASSED` but `manifest.txt` incomplete; §11.4 greppability fails

`resolve.ts:268-282` correctly computes `draftCaptured`/`futureCount`/`compactionResumeHash`/`semanticSummaryEnabled`/`thoughtLoggingEnabled` from `state.activeDraft`/`draftPrompts`/`pendingResume` + `readdir(FUTURE_DIR)` fallback. However `filteredCount: number|undefined = undefined`, `opencodeSessionId/startMs/elapsedMaxMs` at `290-293` are left `undefined/null` until `logging/summary` reducers parse `SYNTHETIC_FILTERED` etc. `grep -n filteredCount diagnostic/hierarchy/resolve.ts` shows definitions, zero assignments from `state|logSummaryInfo`.

`summary.ts:250-258` does `const filtered = (logSummaryInfo as any)?.filteredCount ?? hierarchy.filteredCount; if(filtered!==undefined) lines.push(...)` so when `summarizeQuestJournalLog` fails or `logExists===false` the 4 fields are omitted, violating the 9-field invariant `questHash + draftCaptured + futureCount + compactionResumeHash + semanticSummaryEnabled + thoughtLoggingEnabled + filteredCount + opencodeSessionId + startMs + elapsedMaxMs` (`REMAINING_WORK.md §2.3:106`, `packaging.ts:31` excludes only `manifest.txt` from hashing).

Fix (`§2.3:110`, line-free): populate from `state` with `logSummaryInfo` fallback and unconditionally emit 9 lines (conditional only for `questHash`).

```ts
const draftCaptured = !!state?.activeDraft;
const futureCount = state?.draftPrompts?.length ? state.draftPrompts.length : (try{readdirSync(FUTURE_DIR).filter(f=>f.endsWith(".md")).length}catch{0});
const compactionResumeHash = state?.pendingResume ? createHash("sha256").update(JSON.stringify(state.pendingResume)).digest("hex").slice(0,12) : null;
const filteredCount = (state as any)?.filteredCount ?? logSummaryInfo?.filteredCount ?? 0;
const opencodeSessionId = getSessionId(getActiveContext(ctx)) || (state as any).opencodeSessionId || null;
const startMs = sessionStartMap.get(opencodeSessionId) || (state as any).startMs || null;
const elapsedMaxMs = startMs ? Date.now() - startMs : 0;
```

Note: `summary.ts:242-243` conditional `questHash` keep; `summary.ts:251-258` conditional `if(filtered!==undefined)` means `opencodeSessionId:null` omitted — so “9 fields” only holds when log exists (Bunch 2 nuance). After fix, `summary.ts:209` already emits 9 lines via `logSummaryInfo` fallback when `summarizeQuestJournalLog` succeeds; else `hierarchy` must fill 4 fields. `grep -n filteredCount hierarchy/resolve.ts` showed 5 hits all definitions, zero assignments.

DAG `resolve → state.ts` leaf + `constants.ts:FUTURE_DIR` leaf, +0.

Tests: `diagnostic/summary.ts:209` + `tests/diagnostic_zip.test.ts` `manifest.includes("filteredCount:")` + `manifest.includes("opencodeSessionId:")`.

Verification: `rg "filteredCount:|opencodeSessionId:|startMs:|elapsedMaxMs:" run/manifest.txt` all 4 present even on `logExists===false` cold start (5th `readdir` 0 case); `computeStagedFilesHash` excludes only `manifest.txt`.

Related: #07, #09, #25, #33.
