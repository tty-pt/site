---
id: 12
title: "Save verification Files Modified check is too strict for research-only quests"
state: done
severity: medium
requires: []
validates: "Files Modified check not ERROR on planVersion=1 research-only"
area: "validation/consistency/audit.ts, validation/consistency/checks.ts, validation/helpers.ts"
parent: 44
---
# Issue: Save verification `Files Modified` check is too strict for research-only quests

- **Area:** `pi-quest` validation — `validation/consistency/audit.ts`, `validation/consistency/checks.ts`, `validation/helpers.ts`, `persistence.ts`, `tools/update/executor.ts:364`
- **Runs observed:** `1788280759` at `211 16:41:53.151Z`, `242 16:41:55.028Z`, `248 16:41:55.214Z` `SAVE_VERIFICATION_FAILURE`
- **Severity:** Medium — noisy failure that still advances generation, masking real issues

`audit.ts: SAVE_VERIFICATION` reports `Consistency audit issues in .pi/quest/current/1788280759/quest.md: Files Modified is empty or placeholder, but substantive changes to [detail.c, bud_jsx.h, site_ui.c] were recorded in Latest Reassessment / Completed / tool history. List modified files under Files Modified.` for `planVersion=1` research-only quests where the quest file intentionally has `Files Modified: -` (`Completed:- In Progress:-` at `quest.md:82-83,106`). The check still emits `ERROR code=SAVE_VERIFICATION_FAILURE` yet `persistence.ts` proceeds to `SAVE_VERIFIED gen2 2222f28b` then `gen3 748e8d50`, advancing `saveGeneration` and `lastSavedHash`. The signal is therefore both noisy and ineffective, and it appeared 3 times in interleaved sessions.

Related: #10, #11.

## Re-open evidence — `1788349108` (2026-09-02 11:38–11:45)

5× `SAVE_VERIFICATION_FAILURE` on the main session `01a061e9-…` (turns 25,26,29,34,37) — **all false positives** on a research-only quest that never edited those files. The auditor parsed `Completed` / `Latest Reassessment` / tool-history prose as an edit set and demanded entries under `Files Modified`:

- `:495` (turn 25): `… Files Modified is empty or placeholder, but substantive changes to [list_state.h, list_fill.c, list_json.c, site_ui.h, site_forms.c] were recorded in Latest Reassessment / Completed / tool history`
- `:525` (turn 26): omits `[list_state.h, list_fill.c, list_json.c, site_ui.h, site_forms.c]`
- `:570` (turn 29): omits `[list_state.h, list_fill.c, site_ui.h, site_forms.c, hyle-bud/hyle-bud.h, list_json.c, index.c, design.md, architecture.md]`
- `:634` (turn 34): omits `[external/hyle/include/hyle/schema.h, hyle-bud.h, picker.c, list_json.c]`; `Completed section is empty`
- `:672` (turn 37): omits `[bud/libbud.c, state_macros.h, libbud.c, picker.c, list_json.c]`

Each failure rejected a `quest_update_state` write and contributed to the 19-turn deadlock in `REASSESSMENT_PENDING` (see `FRICTION_REPORT.md` §3.3). Desired fix remains: only require `Files Modified` for files the tool-history confirms were actually edited (via `edit`/`bash` with a write), not files mentioned in prose. Research-only quests with `Files Modified: -` must not error.
