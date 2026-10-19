---
id: 07
title: "Diagnostic hierarchy and packaging miss future-archive and compaction artifacts"
state: done
severity: medium
requires: [04]
validates: "hierarchy futureCount includes future-archive, zip has future/*"
area: "diagnostic/hierarchy/resolve.ts:16-95,268-282, diagnostic/packaging.ts:98-203,296-340, diagnostic/hierarchy/project.ts:4"
parent: 45
---
# Issue: Diagnostic hierarchy and packaging miss `future-archive` and compaction artifacts

- **Area:** `pi-quest` diagnostic — `diagnostic/hierarchy/resolve.ts:16-95,268-282`, `diagnostic/packaging.ts:98-203,296-340`, `diagnostic/hierarchy/project.ts:4`, `constants.ts:3-9`
- **Runs observed:** `1788280759` (`current/1788280759/future-archive/look-consumer-side-code-lot-complexity.md` exists on disk but not required in zip)
- **Severity:** Medium — bundle `Verification: PASSED` does not guarantee draft captured

`hierarchy/resolve.ts:16 discoverRunLogs` scans `current/` only, `collectQuestInfos:61 readdir(current)` and `futureCount` is computed as `state.activeDraft? draftPrompts.length : readdir(FUTURE_DIR).length` — never counts `future-archive/`. `diagnostic/packaging.ts:156-191` correctly copies `future/*.md`, `future-archive/*`, and `compaction-resume.txt` into `current-run/`, and `computeStagedFilesHash:31` excludes only `manifest.txt`, but `verifyDiagnosticZip:297` does not assert that `current-run/future/*.md` exists when `draftCaptured` or `futureCount>0`, nor that `current-run/compaction-resume.txt` exists when `compactionResumeHash` is set. `createUnifiedBundleZip:500` also does not pass `draftCaptured/futureCount/compactionResumeHash` to verification.

At HEAD `2026-09-01` `hierarchy/resolve.ts:268-293` `futureCount` is still `Array.isArray(state.draftPrompts) ? draftPrompts.length : readdir` shadow (always `[]→0` dead code, `future-archive` never counted, `e.isFile` property not `e.isFile()` call) vs `#25`. Manifest `filteredCount/opencodeSessionId/startMs/elapsedMaxMs 290-293` left `undefined` until log parsed vs `#20`. See `#25-futureCount-shadow-and-isFile-bug.md` and `#20-manifest-filteredCount-null.md`.

`project.ts:4 findProjectRoot` walk may include `.pi/extensions/pi-quest/.pi` mirror (`5+2` entries `2026-09-01`) polluting discovery; `packaging.ts:230` skips `.pi` when zipping but hierarchy does not.

Related: #04, #05, #06, #20, #25, #26.
