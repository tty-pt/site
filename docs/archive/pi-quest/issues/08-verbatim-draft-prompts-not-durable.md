---
id: 08
title: "Verbatim draftPrompts are not durably stored"
state: done
severity: high
requires: []
validates: "current/<qid>/draft-prompts.jsonl + future-archive copy exists"
area: "hooks/index.ts:284-302,391, paths.ts:314, diagnostic/packaging.ts:182"
parent: 43
---
# Issue: Verbatim `draftPrompts` are not durably stored

- **Area:** `pi-quest` hooks/persistence — `hooks/index.ts:284-302,391`, `paths.ts:314`, `diagnostic/packaging.ts:182`, `logging/types.ts`
- **Runs observed:** `1788280759` (only `DRAFT_APPENDED {hash,slice(0,80)}` in log, per file-already rule §11.5); also `1788299416` `future/look-consumer-side-code-lot-complexity.md 488B` truncated and `1788299495 future-archive` vs `1788299416` mismatch
- **Severity:** High — refinements irrecoverable if `future/<slug>.md` is deleted

Refinements are kept in `state.draftPrompts: string[]` (`PROMPT_MAX_COUNT=10`, window `[0]+slice(-9)`) and appended to `future/<slug>.md` via `appendToFutureDraft` (`paths.ts:314` regex `/(## Requirements[^\n]*\n)([\s\S]*?)(\n## |\n$)/`). Every mutation calls `persist(pi,ctx)` (`hooks/index.ts:302,347,361,408` + `executor.ts:79,88,367`) so the journal has the array, but on-disk durability is only the single `future/<slug>.md` file and the hash-only `DRAFT_APPENDED` log line. There is no append-only `current/<qid>/draft-prompts.jsonl` (`{ts,hash slice(0,12),slice slice(0,200),len}`) and no copy into `current/<qid>/future-archive/`. If B/C/D promotion deletes `future/` before `packaging.ts:158` copies it, verbatim prompts are gone and cannot be replayed from the hash.

Related: #01, #02, #04, #06.
