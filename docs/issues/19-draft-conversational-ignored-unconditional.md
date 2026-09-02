---
id: 19
title: "DRAFT_CONVERSATIONAL_IGNORED logged unconditionally while activeDraft holds"
state: done
severity: low
requires: []
validates: "DRAFT_CONVERSATIONAL_IGNORED only when activeDraft && CONVERSATIONAL_ACK"
area: "hooks/index.ts:280-337, 336, classification.ts:17"
parent: 43
---
# Issue: `DRAFT_CONVERSATIONAL_IGNORED` logged unconditionally while `activeDraft` holds

- **Area:** `pi-quest` hooks — `hooks/index.ts:280-337` (`336`), `classification.ts:17`, `logging/types.ts`
- **Runs observed:** `1788280759` + HEAD 2026-09-01 (`grep -n DRAFT_CONVERSATIONAL_IGNORED src/hooks/index.ts` 1 hit at `336`)
- **Severity:** Low — audit noise, false positives on every turn

While `state.activeDraft` is set, `hooks/index.ts:258` computes `classification = classifyUserMessage(trimmed)` and `280-320` handles `REFINEMENT_OR_REQUIREMENT` / `QUESTION_OR_DISCUSSION` → `DRAFT_APPENDED`+`DRAFT_APPEND_DEDUPED`. The `try{logEvent("DRAFT_CONVERSATIONAL_IGNORED",…, {hash: createHash("sha256").update(trimmed).digest("hex").slice(0,12), draftPromptsCount})}catch{}` at `334-337` sits **inside `if(activeDraft)` but outside any `if(classification===CONVERSATIONAL_ACK)` guard**. It therefore fires on every turn where `activeDraft` is truthy, even for `REFINEMENT`, `CONFIRMATION`, or non-conversational input, contradicting `REMAINING_WORK.md §2.2:65`.

Intended: emit only when `activeDraft && classification===UserMessageClassification.CONVERSATIONAL_ACK`, then early-return without `appendToFutureDraft`.

Fix (`REMAINING_WORK.md §2.2:82`): move `classifyUserMessage` inside the `if(activeDraft)` branch, guard on `CONVERSATIONAL_ACK`, reuse `node:crypto createHash` already imported.

Ordered-edit (line-free, audit-preserved):

```ts
if (state.activeDraft) {
  const classification = classifyUserMessage(trimmed);
  if (classification === "CONVERSATIONAL_ACK") {
    try { logEvent("DRAFT_CONVERSATIONAL_IGNORED", `conversational ignored`, { hash: createHash("sha256").update(trimmed).digest("hex").slice(0,12), draftPromptsCount: state.draftPrompts.length, classification }); } catch {}
    return;
  }
  if (classification === "REFINEMENT_OR_REQUIREMENT" || classification === "QUESTION_OR_DISCUSSION") {
    // existing REFINEMENT push + appendToFutureDraft + DRAFT_APPENDED branch
  }
}
```

DAG `hooks → logging` leaf, +0 edges.

Tests: `logging_maturity.test.ts:P0` conversational `"hi there"` while `activeDraft` → `c.includes("DRAFT_CONVERSATIONAL_IGNORED")`; non-conversational `"implement auth"` → `!c.includes(...)`; only when `activeDraft && CONVERSATIONAL_ACK` (Appendix A §4: unconditional count 1 vs conditional required).

Verification: `grep -n DRAFT_CONVERSATIONAL_IGNORED src/hooks/index.ts` 1 guarded hit; `rg DRAFT_CONVERSATIONAL_IGNORED .pi/quest/current/*/execution.log` only when `activeDraft && CONVERSATIONAL_ACK`.

Related: #02, #06, #08, #18, #33.
