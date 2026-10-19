---
id: 55
title: "Plan not drafted before review — reviewer approves empty ## Plan: 1."
state: done
severity: high
requires: [46, 50, 53, 54]
validates: "plan_review only triggers when ## Plan has actionable bullet"
area: "critical_agent/policy.ts:maybeTriggerPlanReview + hooks/index.ts:365"
parent: 43
---
# Issue: Plan not drafted before review — reviewer approves empty ## Plan: 1.

- **Area:** `pi-quest` policy — `critical_agent/policy.ts maybeTriggerPlanReview` + `hooks/index.ts:365` threshold — 7874 never drafted `## Plan` before `draft_not_approved`.
- **Runs observed:** `1788307874` 17 evidences, `future/look-consumer…md` only `## Plan: 1.` placeholder, review never `APPROVE` because no plan to validate; agent loop `ask_questions`.
- **Severity:** High — review should not start on empty plan; steer must say `draft plan via quest_update_state {goal,plan,findings}`.
- **Fix:** Gate `plan_review` on `hasActionablePlan` (validatePhasedPlan) — if empty, log `PLAN_NOT_DRAFTED_YET` and skip, steer to draft plan.
