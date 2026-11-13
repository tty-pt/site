---
id: 27
title: "isStoredState guard missing — blind as unknown as StoredState cast + any details + ArchiveContext|{error}"
state: done
severity: medium
requires: [26]
validates: "grep isStoredState 1 hit, Record<string,unknown>"
area: "reconstruction.ts:18-19, types.ts:186,188, lifecycle.ts:212,220"
parent: 44
---
# Issue: `isStoredState` guard missing — blind `as unknown as StoredState` cast + `any` details + `ArchiveContext|{error}`

- **Area:** `pi-quest` reconstruction/types/lifecycle — `reconstruction.ts:18-19`, `types.ts:186,188`, `lifecycle.ts:212,220`, `logging/summary/state.ts:37`
- **Runs observed:** HEAD 2026-09-01 still blind cast, `grep -rn "as unknown as StoredState" src` 1 hit without guard
- **Severity:** Medium — corrupted journal bypasses type safety; `§2.4` `unknown allowed only after isStoredState guard` not satisfied

```ts
// reconstruction.ts:18-19
if(entry.type==="custom" && (entry.customType===CUSTOM_TYPE||entry.customType===LEGACY_CUSTOM_TYPE) && entry.data)
  latest = entry.data as unknown as StoredState; // no validation
```

No `isStoredState` guard vs `REMAINING_WORK.md §2.4` rule `unknown` allowed only after guard. Corrupted journal entry (e.g. partial persistence before kill) is accepted. Similarly:

- `types.ts:186 details?: Record<string,any>` still `any` not `Record<string,unknown>` (`types.ts:188`)
- `lifecycle.ts:212 ArchiveContext|{error}` still `220 if((ctxRes as any).error)` not `if("error" in ctxRes)` discriminated union
- `reconstruction.ts:116-117 semanticSummaryEnabled/thoughtLoggingEnabled` weak assignment without `typeof boolean` guard (unlike `initialPromptLogged:118`) — covered also in #26

Fix (line-free, leaf +0 edges):

```ts
function isStoredState(v: unknown): v is StoredState { return typeof v==="object" && v!==null && ("active" in v || "pendingRootQuest" in v || "activeDraft" in v); }
...
if (entry.type==="custom" && (entry.customType===CUSTOM_TYPE||entry.customType===LEGACY_CUSTOM_TYPE) && entry.data && isStoredState(entry.data)) { latest = entry.data; }
```

Plus `types.ts:details?: Record<string,unknown>` (was `any`) and `lifecycle.ts` discriminated union `ArchiveContext|{error:string}` then `if("error" in ctxRes)` guard not `(ctxRes as any).error`, and `reconstruction.ts:116-117` `semanticSummaryEnabled`/`thoughtLoggingEnabled` with `typeof boolean` guard as `initialPromptLogged` already has.

Verification: `grep isStoredState src/reconstruction.ts` 1 hit after; `grep "as unknown as StoredState" src/reconstruction.ts` guarded; `grep -rn "Record<string, any" src/types.ts` 0 after; `deno check --node-modules-dir=auto` 0 else 1 `npm:@types/node`.

Also covers `§2.4 unknown allowed only after isStoredState guard` rule and `logging/summary/state.ts:37` trivial `StoredState` deletions vs `Record<string,unknown>`.

Related: #26, #28, #29, #33.
