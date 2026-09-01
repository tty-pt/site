---
id: 33
title: "§11.4 + §11.6.4 + §11.7.4 verification matrix not captured as single audit"
state: blocked
severity: medium
requires: [18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 32]
blocked_by: [18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 32]
validates: "unzip -l pi-quest-bundle.zip has current-run/future/* + manifest 9"
area: "33-verification-matrix.md"
---
# Issue: §11.4 + §11.6.4 + §11.7.4 verification matrix not captured as single audit

- **Area:** `pi-quest` diagnostic — `diagnostic/packaging.ts`, `diagnostic/summary.ts`, `diagnostic/hierarchy/resolve.ts`, `logging/*`, `scripts/check-pi-quest-dag.ts`
- **Runs observed:** `1788280759` bundle `Verification: PASSED` but greppability incomplete
- **Severity:** Medium — `grep current-run/execution.log` + `current-run/manifest.txt` + `current-run/future/` contract not checkable from per-issue greps alone

`REMAINING_WORK.md §4` defines the single `grep`-inside-zip contract that survives any `TEST-PROMPT.md` workflow failure (research `gates.ts:PROVISIONAL_RESEARCH_PENDING`, `no-draft→draft` `hooks/index.ts`, single-shot coalesce `pending_coalesce.ts`, `REVISE` re-arm, `APPROVE` `requireConfirm`, synthetic mis-filter `messaging.ts`, compaction orphan, provisional-stall, dedup 40×). After `npm --prefix .pi/extensions/pi-quest run zip`, `diagnostic/packaging.ts:createUnifiedBundleZip` + `verifyDiagnosticZip` + `diagnostic/summary.ts:generateRunManifest` must yield `current-run/manifest.txt` 9 fields + `current-run/future/*.md` + `current-run/future-archive/*.md` + `current-run/compaction-resume.txt` + `current-run/initial-prompt.txt` + `execution.log`.

Matrix (failure point → required `grep` in `current-run/execution.log` inside `pi-quest-bundle.zip` + zip file also + fix):

| Failure point | Required `grep` in `current-run/execution.log` | Zip file also | Fix |
|---|---|---|---|
| `SNAPSHOT_FALLBACK` silent | `SNAPSHOT_FALLBACK` (`grep SNAPSHOT_FALLBACK src/critical_agent/snapshot.ts` 2 hits `git_diff_failed`, `draft_boundary_fallback`) + `grep SNAPSHOT_FALLBACK current-run/execution.log` | — | #18 |
| `DRAFT_CONVERSATIONAL_IGNORED` unconditional | `DRAFT_CONVERSATIONAL_IGNORED` only when `activeDraft && CONVERSATIONAL_ACK` | — | #19 |
| Manifest `filteredCount` null | `grep filteredCount\|opencodeSessionId\|startMs\|elapsedMaxMs current-run/manifest.txt` all 4 present, not null, `questHash + draftCaptured + futureCount + compactionResumeHash + semanticSummaryEnabled + thoughtLoggingEnabled` 9 fields, `computeStagedFilesHash` excludes only `manifest.txt` | `run/manifest.txt` | #20 + #25 |
| 10 silent `return null` | `grep "return null" src/critical_agent/policy.ts` 7 hits → after 0 silent (each `REVIEW_DEDUP_HIT`/`CRITICAL_REVIEW_SUPPRESSED`) + `grep PENDING_COALESCED_DROPPED src/critical_agent/policy/pending_coalesce.ts` | — | #21 |
| `DRAFT_PROMOTED` 0 emitter (23/24) | `grep DRAFT_PROMOTED src --include="*.ts"` 3 emitters after (`commands/promote.ts`, `lifecycle.ts`, `hooks/index.ts`) vs 0 today | `current-run/future-archive/` | #22 |
| `MUTEX_WAIT await import` swallow | `grep "await import" src/utils/mutex.ts` 0 after static import | — | #23 |
| 5th path bare `rename` | `grep DRAFT_DISCARDED src/commands/quest.ts` 1 hit after copy-before-rename | `current-run/future-archive/<slug>.md` includes `quest.ts` path | #24 |
| `futureCount` 0 shadow | `grep futureCount src/diagnostic/hierarchy/resolve.ts` counts `future` + `future-archive`, `state.draftPrompts=[]` → `readdir(FUTURE_DIR)` not dead | `current-run/manifest.txt futureCount` correct | #25 |
| pi-restart orphan | `grep readdirSync.*FUTURE_DIR src/reconstruction.ts` 1 hit + `grep hasDraft src/reconstruction.ts` 3 hits + manual kill/restart `future/*.md` survives → `state.activeDraft==orphan` | `current-run/future/` | #26 |
| `as any` reassessed | `grep -rn "as any" src | wc -l` 0 (allow `state.ts` proxy) + `deno check` 0 + `prettier --check` 0 | — | #28 |
| `try catch` 121 bare → <10 | `grep -rn "catch {}" src | wc -l` <10 + `rg PERSISTENCE_DEGRADED run/execution.log` on failure | — | #29 |
| `isStoredState` guard | `grep isStoredState src/reconstruction.ts` 1 hit + `grep "as unknown as StoredState"` guarded | — | #27 |

Global checks: `deno run --allow-read scripts/check-pi-quest-dag.ts` `DAG gate: passed (130 files, ~688 edges, 2 allowlisted {src/messaging.ts→src/persistence.ts, src/paths.ts→src/markdown.ts→src/markdown_parse.ts})` + `deno test --allow-all` progression `54 passed 302 steps → 55 → 60 passed 330 steps → 62 passed 335 steps` + `npm run zip Verification: PASSED` + `unzip -l | rg current-run/(future|future-archive|compaction-resume|draft-prompts|initial-prompt)`.

Related: all #18-#29, #32.
