---
id: 53
title: "Draft auto-review threshold 7 — trigger plan_review when draftPrompts≥1 && evidence≥7"
state: done
severity: high
requires: [46, 50]
validates: "draft auto-review triggers when draftPrompts≥1 && evidence≥7 (7874)"
area: "hooks/index.ts:365 + state.ts:currentReceipt.evidenceCount"
parent: 43
---
# Issue: Draft auto-review threshold 7 — trigger plan_review when draftPrompts≥1 && evidence≥7

- **Area:** `pi-quest` hooks — `hooks/index.ts:365` auto `if ((draftPrompts.length||0)>=2)` + `state.ts:currentReceipt.evidenceCount` + `research.ts:84`
- **Runs observed:** `1788307874` 17 evidences + 1 draftPrompts never hits ≥2, loop `ask_questions ×3` + `draft_not_approved` + `PROVISIONAL_RESEARCH_PENDING`. `7695` 5 evidences also stuck.
- **Severity:** High — evidence≥7 is perfection per research: 7 distinct file reads = sufficient homework for draft review, avoids waiting for second user refinement which never comes via `ask_questions` tool (not plain prompt).
- **Fix:** `if ((dpLen>=2) || (dpLen>=1 && evidence>=7)) triggerReview` — 7874 17 passes, 7695 5 not (needs 2 prompts), keeps shallow-draft guard.
