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

## Re-open evidence — `1788349108` (2026-09-02 11:38–11:45)

Same wrong-gate class, **additional blocked tool** not covered by the original fix. While `REASSESSMENT_PENDING` the agent's investigation tool was blocked:

- `:703 UNKNOWN_TOOL tool=bash` + `:704 GATE_BLOCKED gate=REASSESSMENT_PENDING` + `:706 IMPLEMENTATION_BLOCKED tool=bash code=UNKNOWN_TOOL_BLOCKED` + `:708 ERROR … [UNKNOWN_TOOL_BLOCKED] Tool 'bash' execution blocked in state REASSESSMENT_PENDING` (turn 40).
- Earlier: `:442/444 GATE_BLOCKED` + `IMPLEMENTATION_BLOCKED tool=edit` (turn 24) — `edit` on `quest.md` was also blocked (`:446`).

The agent was told to complete reassessment (which requires a fresh investigation), but `bash` — its investigation tool — was disallowed in the `revising` gate, so it had no way to satisfy `:565/629/667 "fresh investigation is required after the reassessment trigger"`. Even if the skill hint is now suppressed, the gate still needs to allow `bash` (read/search) and the sanctioned `quest_update_state` path in this state. See new #57.
