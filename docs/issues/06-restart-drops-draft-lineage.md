---
id: 06
title: "pi restart drops draft lineage even though the journal persists it"
state: ready
severity: high
requires: []
blocked_by: []
validates: "kill pi with future/<slug>.md → reconstruct activeDraft"
area: "06-restart-drops-draft-lineage.md"
---
# Issue: `pi` restart drops draft lineage even though the journal persists it

- **Area:** `pi-quest` persistence/reconstruction — `persistence.ts:15-46`, `state.ts:111-132,228-243`, `reconstruction.ts:14-114`, `diagnostic/hierarchy/resolve.ts:268-281`, `index.ts:77-81`
- **Runs observed:** `1788280759` (`state.activeDraft==null` orphan after kill, `future/<slug>.md` survives on disk)
- **Severity:** High — full process not recoverable, draft refinements not replayable

`persistence.ts:19 persist(pi,ctx)` calls `pi.appendEntry(CUSTOM_TYPE, snapshotState)` durably persisting `activeDraft:228, draftPrompts:229, draftCreatedAt:230, draftLastSavedHash:231, draftLastReviewKey:232, semanticSummaryEnabled:233, thoughtLoggingEnabled:234, initialPromptLogged:235, last*ReviewRequestKey, awaitingReview` (`state.ts:111-132`). The file `future/<slug>.md` also survives on disk via `paths.ts:330 writeFile`.

However `reconstruction.ts:24-102 restoreSessionState` never reads those fields and resets them to `createDefaultState:44 null/[]/false`. `reconstruct:112` gates on `latest.active || latest.pendingRootQuest` only, so a draft-only qid before `quest.md` exists falls back to `createDefaultState`. After `session_start`/`session_tree` (`index.ts:77,81 reconstruct()` → `reconcileDerivedState syncImplementationPermission false`) `state.activeDraft==null` is orphan, `hierarchy/resolve.ts:269 draftCaptured` is false, `handleTurnEnd 100-122` early-returns, and hash-only `DRAFT_APPENDED` cannot replay `draftPrompts` array. No test asserts `activeDraft/draftPrompts` survive `restoreSessionState` (`grep tests -r draft` hits only `logging_maturity.test.ts:27` and `reviewer_naming_synthetic:123`).

At HEAD `2026-09-01` 11 fields are hydrated as of Appendix B §1 table:

`activeDraft`, `draftPrompts` (hydrate `if(activeDraft && draftPrompts.length===0) readFileSync(FUTURE_DIR/activeDraft.md) → parse Requirements` ), `draftCreatedAt`, `draftLastSavedHash`, `draftLastReviewKey`, `semanticSummaryEnabled` (weak — no `typeof boolean` vs `initialPromptLogged` which has it), `thoughtLoggingEnabled` (same weak), `initialPromptLogged` (guarded), `lastPlanReviewRequestKey`, `lastDraftReviewRequestKey`, `awaitingReview` + `lastDirectionReviewKey/At` — file fallback done but scoped. Orphan when `activeDraft==null` yet `future/<slug>.md` exists remains irretrievable: `hasDraft` checks journal only, `readdirSync(FUTURE_DIR)` never run, relative `readFileSync(FUTURE_DIR/activeDraft.md)` vs `resolve(projectRoot,FUTURE_DIR)` inconsistency, and weak guards. Also blind `as unknown as StoredState` without `isStoredState` and `draft-prompts.jsonl` never rehydrated on restart (B′). See #26 and #27.

Related: #01, #03, #04, #05, #07, #08, #18, #24, #25, #26, #27.
