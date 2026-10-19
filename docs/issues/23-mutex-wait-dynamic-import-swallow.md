---
id: 23
title: "MUTEX_WAIT still await import with empty catch{} swallowing logger failure"
state: done
severity: low
requires: []
validates: "grep await import src/utils/mutex.ts 0 after static import"
area: "utils/mutex.ts:20,30, logging/core.ts, logging/summary/reducers.ts:138"
parent: 44
---
# Issue: `MUTEX_WAIT` still `await import` with empty `catch{}` swallowing logger failure

- **Area:** `pi-quest` utils — `utils/mutex.ts:20,30`, `logging/core.ts`, `logging/summary/reducers.ts:138`
- **Runs observed:** HEAD 2026-09-01 (`grep -n "await import" src/utils/mutex.ts` 2 hits at `20,30`, both `catch{}`)
- **Severity:** Low — contention observability lost, `PERSISTENCE_DEGRADED` never surfaces

```ts
// utils/mutex.ts:20 and :30
try{
  const {logEvent}=await import("../logging/core.ts");
  logEvent("MUTEX_WAIT",`mutex wait ${key}`,{lockKey:key,waitMs} as any);
}catch{}
...
try{
  const {logEvent}=await import("../logging/core.ts");
  logEvent("MUTEX_ACQUIRED",`mutex acquired ${key}`,{lockKey:key,holdMs,waitMs,contention:hadContention} as any);
}catch{}
```

`try{await import} catch{}` empty swallows both dynamic-import failure and `logEvent` failure, no `logError PERSISTENCE_DEGRADED`. Not `void async import` but same `catch{}` pattern (`REMAINING_WORK.md §2.10:184`). Already `MUTEX_WAIT 1` / `MUTEX_ACQUIRED 1` emitters exist but lose `hadContention` enrichment when swallowed.

Fix (`§2.10:199`): add `import {logEvent} from "../logging.ts"` static leaf (allowlisted `messaging↔persistence` cycle already allowlisted), replace both blocks with `try{logEvent("MUTEX_WAIT",…, {lockKey, waitMs})}catch{}` / `try{logEvent("MUTEX_ACQUIRED",…, {lockKey, holdMs, waitMs, contention:hadContention})}catch{}` — no dynamic import.

DAG `utils/mutex.ts → logging.ts` leaf stays within allowlist, +0 edges.

Verification: `rg "await import" src/utils/mutex.ts` 0 after; `rg MUTEX_WAIT .pi/quest/current/*/execution.log` on contention; `logging_maturity.test.ts:P3b` already asserts `MUTEX_WAIT hadContention` + `MUTEX_ACQUIRED always holdMs/waitMs/contention` (Bunch 2 notes `MUTEX_WAIT` lacks `hadContention` enrichment — ensure `waitMs` also in `logSummary` counters).

Related: #29, #17, #33.
