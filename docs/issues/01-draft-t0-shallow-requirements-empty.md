---
id: 01
title: "Draft at t=0 is shallow — Requirements stays empty"
state: ready
severity: medium
requires: []
validates: "future/<slug>.md Requirements prefilled from prompt slice"
area: "markdown/template/header.ts:36, paths.ts:300,321"
parent: 43
---
# Issue: Draft at t=0 is shallow — Requirements stays empty

- **Area:** `pi-quest` persistence — `markdown/template/header.ts:36`, `paths.ts:300,321`
- **Runs observed:** `1788280759` (`future/look-consumer-side-code-lot-complexity.md` 488 B), also `1788278057`
- **Severity:** Medium — rich context lost before first `quest_update_state`

`FUTURE_QUEST_TEMPLATE(slug, prompt)` writes the user prompt into `## Goals & Scope` only; `## Requirements` is hard-coded as `-`. `createFutureDraftFromPrompt` (`paths.ts:321`) creates `FUTURE_DIR/<slug>.md` with that template and does not pre-fill `Requirements` from the prompt. On disk the t=0 draft therefore contains only slug + goals, e.g. `future/look-consumer-side-code-lot-complexity.md` observed 2026-09-01 has empty `Requirements: -`. Any why-now / constraints / files-examined supplied in the initial prompt are not captured durably.

No other stage enriches the file until `appendToFutureDraft` is called with a refinement.

Related: #02, #04, #06, #08.
