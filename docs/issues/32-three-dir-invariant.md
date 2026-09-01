---
id: 32
title: "3-dir invariant not stated as single normative table"
state: blocked
severity: medium
requires: [05, 06, 07, 17]
blocked_by: [05, 06, 07, 17]
validates: "ls .pi/quest 3 dirs only, no QUEST_RUN_DIR"
area: "32-three-dir-invariant.md"
---
# Issue: 3-dir invariant not stated as single normative table

- **Area:** `pi-quest` architecture — `constants.ts`, `diagnostic/packaging.ts`, `diagnostic/hierarchy/*`, `lifecycle/archive/removal.ts`, `logging/paths.ts`, `hooks/index.ts`
- **Runs observed:** `2026-09-01` `read .pi/quest` showed `runs/` empty, `finalized_logs/` 209 dups, `.pi/extensions/pi-quest/.pi` mirror 5+2 entries
- **Severity:** Medium — `Verification: PASSED` with polluted discovery or orphaned dirs

`REMAINING_WORK.md §3` and `§0` define the only 3 dirs:

- `QUEST_ROOT` (`.pi/quest`) + `QUEST_CURRENT_DIR` (`.pi/quest/current` questId-sharded via `paths.ts:questDirPath` + `state.ts:generateQuestId`) + `QUEST_ARCHIVE_DIR` (`.pi/quest/archive` `.zip`) — leaf `constants.ts`
- `FUTURE_DIR` (`.pi/quest/future`) draft staging + `current/<qid>/future-archive/` + `draft-prompts.jsonl` + `compaction-resume.txt` (`hooks/index.ts`)

Invariants:

- No `QUEST_RUN_DIR`, no `runs/` — empty → `rmdir` (already empty at `read .pi/quest`).
- `finalized_logs/` 209 dups from `lifecycle/archive/removal.ts:pinLogToFinalized` → delete on archive (currently only writes); `logging/paths.ts` fallback is `current` first then `finalized_logs` legacy, not `run/` choice 3.
- `current-run/` inside `pi-quest-bundle.zip` is a view of `current/<qid>/` (plus `future/`, `future-archive/`, `compaction-resume.txt`, `draft-prompts.jsonl`), not `run/<qid>/`.
- Hash helper reused: `node:crypto createHash('sha256').update(s,'utf8').digest('hex').slice(0,12)` already in `diagnostic/packaging.ts:computeFileSha256` + `hooks/handlers.ts` + `tools/update/executor.ts` — no new dep. File-already rule: when markdown already in `run/future/*.md` / `compaction-resume.txt` / `initial-prompt.txt`, `execution.log` carries only `hash`+`ref` or `slice(0,80)` — no duplication.
- DAG: `logging/types.ts` leaf; `config.ts` imports only `getCachedSettingsJson` (`utils/cache.ts`) + `env` + `constants.ts`; `reconstruction.ts` → `state.ts` + `constants.ts:FUTURE_DIR` leaf; `messaging→logging` static import stays within allowlisted `messaging↔persistence` cycle (`check-pi-quest-dag.ts` stays `passed`).

`project.ts:findProjectRoot` walk must skip `extensions/` prefix — test mirror `.pi/extensions/pi-quest/.pi` → `tests/.pi-fixture` via skip, `diagnostic/packaging.ts` already skips `.pi` when zipping but `hierarchy` must too. Also `diagnostic/packaging.ts:createRunDirectory` already copies `future/` + `future-archive` + `compaction-resume.txt` + `draft-prompts.jsonl` (`hooks/index.ts:join(questDirPath ...)`) per Gap 3 — verify via `diagnostic_zip`.

Additional Appendix B gaps preserved: `markdown/template/header.ts:Requirements` enrichment `prompt.slice(0,120)` (`goal.trim().slice(0,120)`) and `state.ts:draftLastSavedHash` setter (`hash` set from `futureDraftPath`) — already DONE but not in per-issue greps; `hooks/index.ts:draft-prompts.jsonl` done but never rehydrated on restart (B′ — see #26).

Verification: `ls .pi/quest` has 3 dirs only; `rg -n "QUEST_RUN_DIR" src` 0; `rg finalized_logs src` then `grep -c pinLogToFinalized`; `deno run --allow-read scripts/check-pi-quest-dag.ts` `DAG gate: passed (130 files, ~688 edges, 2 allowlisted)`.

Related: #05, #07, #17.
