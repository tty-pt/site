---
id: 54
title: "Logs insufficient — missing DRAFT_AUTO_REVIEW_CHECK, draftNotApprovedDetails, UI_STATUS"
state: done
severity: medium
requires: [46, 50, 53]
validates: "execution.log contains DRAFT_AUTO_REVIEW_CHECK + UI_STATUS during drafting"
area: "hooks/index.ts:363 + tools/update/executor.ts:386 + hooks/handlers.ts:332 + ui.ts:25"
parent: 43
---
# Issue: Logs insufficient — missing DRAFT_AUTO_REVIEW_CHECK, draftNotApprovedDetails, UI_STATUS

- **Area:** `pi-quest` logs — `hooks/index.ts:363` threshold, `executor.ts:386 REVIEW_DEDUP_HIT`, `handlers.ts:332 no_active_quest`, `ui.ts:updateUIStatus` — 7874 archived 8329 shows 17 evidences but no `PLAN_REVIEW_REQUESTED`, no `DRAFT_STATE`, no `params.name` on `no_active_quest`.
- **Runs observed:** `1788307874` 17 evidences + 1 draftPrompts, `future/look-consumer…md ## Plan: 1.` placeholder, 0 `STATE_UPDATE_ACCEPTED`, 1 `REVIEW_DEDUP_HIT` with only `hash/boundaryKey`.
- **Severity:** Medium — hides why `dpLen=1 evidence=17` didn’t trigger review and why `no_active_quest` empty.
- **Fix:** Always log `DRAFT_AUTO_REVIEW_CHECK dpLen/evidence/valid`, enrich `REVIEW_DEDUP_HIT` with `dpLen/evidence/hasReviewer/isDraftReviewValid`, log `paramsKeys` on `no_active_quest`, add `UI_STATUS` per turn.
