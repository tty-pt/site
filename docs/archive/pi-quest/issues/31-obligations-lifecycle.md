---
id: 31
title: "Obligations lifecycle cross-link missing"
state: deferred
severity: low
requires: [33]
validates: "obligations doc cross-link present"
area: "types.ts, transitions.ts, state.ts"
parent: 45
---
# Issue: Obligations lifecycle cross-link missing

- **Area:** `pi-quest` obligations — `types.ts`, `transitions.ts`, `state.ts`, `messaging.ts`, `reconstruction.ts`
- **Runs observed:** `1788280759` — code DONE per audit, docs 0 hits
- **Severity:** Low — concept implemented but not linked to gap-fill plans

`CONCEPT_OBLIGATIONS_LIFECYCLE.md` 52 lines defined `ObligationStatus` 6 variants in `types.ts` with transitions `fulfill`/`supersede`/`cancel`/`fail`/`reconcile` in `transitions.ts`, `obligationHistory` with `MAX_HISTORY_SIZE=50` in `types.ts` + `state.ts`, `drainAgentObligations` in `messaging.ts` delivering until `fulfill`, and `reconstructObligationHistory` in `reconstruction.ts`. Implementation is DONE per adversarial audit, but `rg fulfillObligation docs/` is 0 — missing cross-link to `HIGH_LEVEL_PLAN` docs.

Per `REMAINING_WORK.md §2.7`, keep as 1-line note `P2 integrate after gap-fill green`, not detailed now, with follow-on mapping `Remaining §2.7 | Original CONCEPT_OBLIGATIONS_LIFECYCLE.md | Next epics deferred archive refs`.

Original source `CONCEPT_OBLIGATIONS_LIFECYCLE.md` to be moved to `docs/archive/` after confirmation (single source becomes `REMAINING_WORK.md` then this issue).

Verification: `rg -n "ObligationStatus|fulfillObligation|reconstructObligationHistory" src --include="*.ts"` hits; `rg fulfillObligation docs/` currently 0 — after fix, cross-link present.

Related: #30, #16.
