---
id: 25
title: "futureCount 0 when disk has drafts — Array.isArray([]) shadows readdir, misses future-archive, e.isFile"
state: done
severity: medium
requires: [07]
validates: "resolve.ts readdir + future-archive => futureCount==5 on fixture"
area: "diagnostic/hierarchy/resolve.ts:268-293, 273, diagnostic/packaging.ts:157-196,297-340"
parent: 45
---
# Issue: `futureCount` 0 when disk has drafts — `Array.isArray([])` shadows `readdir`, misses `future-archive`, `e.isFile` property not call

- **Area:** `pi-quest` diagnostic hierarchy — `diagnostic/hierarchy/resolve.ts:268-293` (`273`), `diagnostic/packaging.ts:157-196,297-340`, `constants.ts:9 FUTURE_DIR`, `constants.ts:5 QUEST_CURRENT_DIR`
- **Runs observed:** `1788280759` under-report `futureCount` 0 despite `future/3.md` + `current/<qid>/future-archive/2.md` on disk; HEAD 2026-09-01 still dead code
- **Severity:** Medium — manifest `futureCount` / `draftCaptured` under-report hides #24 loss; `verifyDiagnosticZip:339` not requiring `run/future/*.md`

```ts
// hierarchy/resolve.ts:268-281
let draftCaptured=false; let futureCount=0;
if(state?.activeDraft) draftCaptured=true;
if(Array.isArray(state?.draftPrompts)) futureCount=state.draftPrompts.length; // always true even when []→0, dead code below
else { futDir=resolve(projectRoot,FUTURE_DIR); ents=readdir; futureCount=ents.filter(e.isFile && .md).length } // never runs, also e.isFile property not e.isFile() call
// never touches future-archive
```

- `state.draftPrompts` defaults `[] state.ts:112` → `Array.isArray([])` true → `futureCount=0` always, `readdir(FUTURE_DIR)` dead.
- `future-archive/` never counted though `packaging.ts:170-178` copies it to `run/future-archive/`.
- `e.isFile` vs `e.isFile()` bug (property not invocation) noted `REMAINING_WORK.md Bunch 2 Appendix B′` + `§2.12:233`.

Fix (`REMAINING_WORK.md §2.12:248`):

```ts
let draftCaptured = !!state?.activeDraft;
let futureCount = 0;
if(state?.draftPrompts?.length) futureCount=state.draftPrompts.length;
else { try{ const futDir=resolve(projectRoot,FUTURE_DIR); const ents=await readdir(futDir,{withFileTypes:true}); futureCount+=ents.filter(e=>e.isFile() && e.name.endsWith(".md")).length;}catch{} }
try{ const archDir=resolve(projectRoot,QUEST_CURRENT_DIR, activeQid||"", "future-archive"); const aEnts=await readdir(archDir,{withFileTypes:true}); futureCount+=aEnts.filter(e=>e.isFile() && e.name.endsWith(".md")).length;}catch{}
if(futureCount>0) draftCaptured=true;
```

Imports `QUEST_CURRENT_DIR`+`FUTURE_DIR` already leaf, `createHash` for `compactionResumeHash:282`. DAG +0.

Verification: `state.draftPrompts=[]` + `future/3.md` + `current/<qid>/future-archive/2.md` → `futureCount==5`; `rg futureCount src/diagnostic/hierarchy/resolve.ts -n` hits `future-archive`; `rg "e.isFile()" hierarchy/resolve.ts` 2 hits not `e.isFile`.

Related: #07, #20, #24, #26.
