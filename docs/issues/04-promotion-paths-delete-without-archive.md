---
id: 04
title: "Promotion paths delete the only copy of future/<slug>.md (5 paths; D null-qid edge)"
state: ready
severity: high
requires: []
blocked_by: []
validates: "all 5 paths archive-before-unlink + DRAFT_DISCARDED"
area: "04-promotion-paths-delete-without-archive.md"
---
# Issue: Promotion paths delete the only copy of `future/<slug>.md` (5 paths; D null-qid edge)

- **Area:** `pi-quest` lifecycle — `tools/update/executor.ts`, `lifecycle.ts`, `commands/promote.ts`, `commands/quest.ts`, `paths.ts`, `diagnostic/packaging.ts`
- **Runs observed:** `1788280759` + HEAD 2026-09-01 still bare via `commands/quest.ts`
- **Severity:** High — irretrievable draft loss before `packaging.ts` copies `future/`

Five paths convert `future/<slug>.md` → `current/<qid>/quest.md`:

- **A** `quest_update_state` (`tools/update/executor.ts` `syncQuestIdentity`) — now correct: `mkdir future-archive` + `copyFileSync` + `logEvent DRAFT_DISCARDED {hash slice(0,12),dest}` + `unlink`.
- **B** `/quest` rename (`lifecycle.ts:activateExistingQuest`) — now fixed: `copyFileSync` before `rename`.
- **C** `/quest-promote` (`commands/promote.ts:promoteDraft`) — now fixed: `copyFile` before `unlink`.
- **D** `cleanDraftIfExists` (`paths.ts`) — now archive-before-unlink but edge: `qid null` → `questDirPath(null)→""` → `join("","future-archive")` relative `./future-archive` if `getQuestId(ctx)` null before `questId` established (Appendix B §2 D null-qid edge).
- **E** `commands/quest.ts` (`await rename(futurePath,path)`) — **still bare**, 0 `future-archive` copy, 0 `DRAFT_DISCARDED`, `cleanDraftIfExists(name)` finds no file after move → `run/future-archive/<slug>.md` lost. See #24 (copy-before-rename canon from `lifecycle.ts`).

If bundle created after any bare path before copy, `diagnostic/packaging.ts` has nothing to copy; only `hash`+`slice(0,80)` in `execution.log` (file-already rule) remains. Covers `REMAINING_WORK.md §2.11` + Appendix B §2 table and B′ additional gaps (`draft-prompts.jsonl` done but never rehydrated on restart — see #26; `finalized_logs` delete claim not implemented — see #32).

Related: #01, #06, #07, #08, #22, #24, #25, #26, #32, #33.
