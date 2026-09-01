---
id: 03
title: "draftLastSavedHash is defined but never set"
state: ready
severity: medium
requires: []
validates: "state.draftLastSavedHash == sha256(future file) slice12"
area: "state.ts:111-114,228-232, hooks/index.ts:391-408, persistence.ts:19"
parent: 43
---
# Issue: `draftLastSavedHash` is defined but never set

- **Area:** `pi-quest` state — `state.ts:111-114,228-232`, `hooks/index.ts:391-408`, `persistence.ts:19`
- **Runs observed:** `1788280759` (`state.ts:114 draftLastSavedHash null` throughout)
- **Severity:** Medium — hash-only log cannot be correlated to file content

`StoredState` declares `draftLastSavedHash: string|null` (`state.ts:114`) and `snapshotState:228-232` persists it via `pi.appendEntry(CUSTOM_TYPE, snapshotState)` on every `persist(pi,ctx)`. However `hooks/index.ts:391 createFutureDraftFromPrompt` sets `state.activeDraft=slug` without computing `sha256(fileContent).slice(0,12)`, and `appendToFutureDraft` (`paths.ts:314`) reads/writes the file without updating the hash. The field therefore remains `null` (observed in all `SESSION_SNAPSHOT` lines for `1788280759`), so `DRAFT_APPENDED {hash}` cannot be verified against the actual `future/<slug>.md` content.

The hash helper `node:crypto createHash('sha256').update(s,'utf8').digest('hex').slice(0,12)` already exists in `diagnostic/packaging.ts:2`, `hooks/handlers.ts:1`, `tools/update/executor.ts:1` but is not used here.

Related: #01, #02, #06.
