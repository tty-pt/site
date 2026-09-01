---
id: 35
title: "8 to-be-removed docs → archive map not preserved"
state: ready
severity: low
requires: []
blocked_by: []
validates: "docs/archive move map 8 files documented"
area: "35-archive-map.md"
---
# Issue: 8 to-be-removed docs → archive map not preserved

- **Area:** `pi-quest` docs — `.pi/extensions/pi-quest/docs/` to `docs/archive/` move
- **Runs observed:** `2026-09-01` `git status --porcelain` showed 8 untracked plan files with `??` (now replaced by `REMAINING_WORK.md` alone)
- **Severity:** Low — audit trail for `docs/archive/` move after confirmation

`REMAINING_WORK.md §7` contained the only map of which 8 files can be moved to `docs/archive/` after `REMAINING_WORK.md` confirmation, with lines + what was done + what remains. No per-issue file had it — would be lost on deletion.

| To-be-removed file | Lines | What was already done (archive reason) | What remains |
|---|---|---|---|
| `HIGH_LEVEL_PLAN_V2_LOGGING_FIRST.md` | 198 lines | L0-L3b P0-P3b 20 features — 17 DONE per §1 | §2.3 manifest null + §2.1-2.2 |
| `HIGH_LEVEL_PLAN_V2_LOGGING_FIRST_EXPANDED.md` | 254 lines | Expanded L4/L5 flags `semanticSummaryEnabled` choice 3 `run/future/` — both `false` landed | §2.3 manifest |
| `HIGH_LEVEL_PLAN_V2_GAPFILL_DETAILED.md` | 621 lines | 7 gaps + 3 dirs — 90% DONE, `runs run/` proposal reverted to `current` 3 dirs | §2.1-2.3 remaining 2 gaps + bug |
| `HIGH_LEVEL_PLAN_V2_GAPFILL_AND_CACHE_REORG.md` | 185 lines | Gap 1–7 table + 3-dir `§3.3` mapping — 90% DONE | §2.1-2.3 |
| `HIGH_LEVEL_PLAN_CONFIGURABLE_REVIEWER.md` | 210 lines truncated (orig 743) `B3 DONE` claim | B2/B2.5/B3 56 passed DONE, B3.1 `INITIAL_PROMPT` partial, C/D deferred | §2.6 Epics A-D 1-line refs (#30) |
| `CONCEPT_OBLIGATIONS_LIFECYCLE.md` | 52 lines | `ObligationStatus` 6 + `fulfill/reconcile` code DONE, docs 0 hits | §2.7 (#31) |
| `PLAN_REMOVE_AS_ANY.md` | 108 lines `src:11` claim false | `src:213` regress, Phases 1-4 stale | §2.4 reassessed (#28) |
| `ARCHITECTURE_MAP.md` | 9509 bytes | `logging.ts` `state.ts` map correct but missing V2 draft maps | §2.5 try-catch (#29) |

Also replaced `HIGH_LEVEL_PLAN_V2_LOGGING_FIRST_EXPANDED.md:5 254 lines` + `HIGH_LEVEL_PLAN_V2_GAPFILL_DETAILED.md:3 621 lines 57K` + `HIGH_LEVEL_PLAN_V2_GAPFILL_AND_CACHE_REORG.md:3 185 lines 22K` + `HIGH_LEVEL_PLAN_CONFIGURABLE_REVIEWER.md:3 210 lines truncated (orig 743 B3 DONE claim)` + `CONCEPT_OBLIGATIONS_LIFECYCLE.md:1 52 lines` + `PLAN_REMOVE_AS_ANY.md:1 108 lines` + `ARCHITECTURE_MAP.md:1 9509 bytes`.

Constraint: no `htdocs/*.js`, no `mods/*/ux`, no schema migration. D must not re-prune `constants.ts` (already pruned C P3) — verify only.

Related: #30, #31, #28, #29, #32.
