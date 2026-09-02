---
id: 52
title: "Skill hint CALL quest_update_state shown while gate blocks it (REASSESSMENT_PENDING + draft_not_approved)"
state: done
severity: low
requires: [51]
validates: "Skill: quest_journal CALL quest_update_state must not show when reassessmentRequired"
area: "hooks/index.ts:546-551 skillHint + execution.log:9-28"
parent: 43
---
# Issue: Skill hint CALL quest_update_state shown while gate blocks it (REASSESSMENT_PENDING + draft_not_approved)

- **Area:** `pi-quest` hooks — `hooks/index.ts:546-551` (`skillHint = (!state.active && (pending||draft)) ? CALL quest_update_state`) + `execution.log:9-28` `1788305314`
- **Runs observed:** `1788305314` `hooks/index.ts:547` emits imperative `CALL quest_update_state on turn 1` even when `REASSESSMENT_PENDING` (`81-86`) and `PLAN_REVIEW_UNCERTAIN` (`24`) holds; agent then hits `113 draft_not_approved` failure loop. Hint should be conditional on `!reassessmentRequired && isDraftReviewValid/researchComplete`.
- **Severity:** Low — opposite of #40 never shown; this is shown at wrong gate, should be `await reviewer verdict, reads allowed`.

Related: #40 (skill not invoked ready), #51, #46.
