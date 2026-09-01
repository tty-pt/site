---
id: 12
title: "Save verification Files Modified check is too strict for research-only quests"
state: ready
severity: medium
requires: []
blocked_by: []
validates: "Files Modified check not ERROR on planVersion=1 research-only"
area: "12-save-verification-strict-files-modified.md"
---
# Issue: Save verification `Files Modified` check is too strict for research-only quests

- **Area:** `pi-quest` validation — `validation/consistency/audit.ts`, `validation/consistency/checks.ts`, `validation/helpers.ts`, `persistence.ts`, `tools/update/executor.ts:364`
- **Runs observed:** `1788280759` at `211 16:41:53.151Z`, `242 16:41:55.028Z`, `248 16:41:55.214Z` `SAVE_VERIFICATION_FAILURE`
- **Severity:** Medium — noisy failure that still advances generation, masking real issues

`audit.ts: SAVE_VERIFICATION` reports `Consistency audit issues in .pi/quest/current/1788280759/quest.md: Files Modified is empty or placeholder, but substantive changes to [detail.c, bud_jsx.h, site_ui.c] were recorded in Latest Reassessment / Completed / tool history. List modified files under Files Modified.` for `planVersion=1` research-only quests where the quest file intentionally has `Files Modified: -` (`Completed:- In Progress:-` at `quest.md:82-83,106`). The check still emits `ERROR code=SAVE_VERIFICATION_FAILURE` yet `persistence.ts` proceeds to `SAVE_VERIFIED gen2 2222f28b` then `gen3 748e8d50`, advancing `saveGeneration` and `lastSavedHash`. The signal is therefore both noisy and ineffective, and it appeared 3 times in interleaved sessions.

Related: #10, #11.
