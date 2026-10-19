---
id: 26
title: "pi-restart orphan future/<slug>.md without activeDraft irretrievable (extends #06)"
state: done
severity: high
requires: [06]
validates: "reconstruction orphan activeDraft==slug when future file exists"
area: "reconstruction.ts:24-148, 28-37, 137-141"
parent: 43
---
# Issue: pi-restart orphan `future/<slug>.md` without `activeDraft` irretrievable (extends #06)

- **Area:** `pi-quest` reconstruction — `reconstruction.ts:24-148` (`28-37`, `137-141`), `persistence.ts:15-46` `pi.appendEntry(CUSTOM_TYPE,snapshotState)`, `state.ts:111-132,228-243`, `constants.ts:9 FUTURE_DIR`
- **Runs observed:** `1788280759` kill before `persist:19` flush; `state.activeDraft==null` orphan though `future/<slug>.md` survives on disk; HEAD hydrate only `if(activeDraft && draftPrompts.length===0)`
- **Severity:** High — full process not recoverable, refinements not replayable; hash-only `DRAFT_APPENDED` cannot replay `draftPrompts` array

`persistence.ts:19` durably persists `activeDraft/draftPrompts/draftCreatedAt/draftLastSavedHash/...` via journal. File `future/<slug>.md` survives via `paths.ts:330 writeFile`. But `reconstruction.ts:24-122 restoreSessionState` at HEAD hydrates only

```ts
if(activeDraft && draftPrompts.length===0){ readFileSync(`${FUTURE_DIR}/${activeDraft}.md`) → parse Requirements "- " → draftPrompts }
```

If `activeDraft==null` (killed before journal flush or `latest==undefined` → `createDefaultState()` ) the orphan `future/<slug>.md` is never scanned. Gate `140-141 hasDraft = !!latest.activeDraft || !!latest.draftPrompts.length` inspects journal only, not disk, so falls back to `createDefaultState:44 null/[]/false`, `hierarchy/resolve.ts:269 draftCaptured` false, `reconstruct:112 latest.active||pendingRootQuest` drops draft-only qid. Also `reconstruction.ts:33` uses bare relative `` `${FUTURE_DIR}/${activeDraft}.md` `` vs `packaging.ts:158 resolve(projectRoot,FUTURE_DIR)` (`§2.13:552` missed `projectRoot` inconsistency) and `116-117 semanticSummaryEnabled/thoughtLoggingEnabled` weak guard without `typeof boolean`.

Fix (broaden orphan fallback — line-free, preserves `projectRoot` vs `cwd` inconsistency note):

```ts
let activeDraft: string|null = typeof latest.activeDraft === "string" ? latest.activeDraft : null;
let draftPrompts: string[] = Array.isArray(latest.draftPrompts) ? latest.draftPrompts : [];
if (!activeDraft) {
  try { const ents = readdirSync(FUTURE_DIR, {withFileTypes:true}); const md = ents.filter(e=>e.isFile() && e.name.endsWith(".md")).map(e=>e.name.replace(/\.md$/,"")).sort(); if(md.length) activeDraft=md[0]; } catch {}
}
if (activeDraft && draftPrompts.length===0) {
  try { const c = readFileSync(`${FUTURE_DIR}/${activeDraft}.md`, "utf8"); const req = c.match(/## Requirements([\s\S]*?)(?:\n## |\n$)/)?.[1] || ""; const items = req.split("\n").filter(l=>l.trim().startsWith("- ")).map(l=>l.replace(/^-+\s*/,"").trim()).filter(Boolean); if(items.length) draftPrompts=items; } catch {}
}
// semanticSummaryEnabled / thoughtLoggingEnabled with typeof boolean guard as initialPromptLogged already has:
// semanticSummaryEnabled: typeof latest.semanticSummaryEnabled === "boolean" ? latest.semanticSummaryEnabled : undefined
const hasDraftOnDisk = (()=>{try{return readdirSync(FUTURE_DIR).filter(f=>f.endsWith(".md")).length>0}catch{return false}})();
const hasDraft = !!activeDraft || !!draftPrompts.length || hasDraftOnDisk;
const reconstructedState = latest && (latest.active || latest.pendingRootQuest || hasDraft) ? restoreSessionState(latest) : createDefaultState();
```

Notes: hydrate only scoped `if(activeDraft && draftPrompts.length===0)` today misses orphan; `hasDraft` journal-only (`latest.activeDraft || draftPrompts.length`) drops `latest==null` orphan; `reconstruction.ts:33` uses bare relative `${FUTURE_DIR}/${activeDraft}.md` vs `packaging.ts:resolve(projectRoot,FUTURE_DIR)` inconsistency; `hooks/index.ts:draft-prompts.jsonl` (`join(questDirPath(qid),"draft-prompts.jsonl")`) done but never rehydrated on restart (Bunch 2 B′ gap). Also `sessionManager.getBranch()` may return empty when `pi` killed before `persist:19`.

Add `existsSync/readdirSync/readFileSync` + `FUTURE_DIR` leaf imports, DAG +0.

Tests: `tests/reconstruction_draft.test.ts` add `future/<orphan>.md` exists + `latest==null` → `reconstruct(ctx).activeDraft===orphan`.

Verification: `future/<orphan>.md` exists + `latest==null` → `reconstruct(ctx).activeDraft===orphan`; `grep readdirSync.*FUTURE_DIR src/reconstruction.ts` 1 hit + `grep hasDraft` 3 hits; kill-before-flush `future/*.md` survives.

Related: #06, #01, #04, #25, #27, #33.
