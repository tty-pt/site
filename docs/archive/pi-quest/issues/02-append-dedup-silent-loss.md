---
id: 02
title: "Refinement dedup is silent"
state: done
severity: low
requires: []
validates: "execution.log:DRAFT_APPEND_DEDUPED on duplicate slice"
area: "paths.ts:291-319, hooks/index.ts:284-292"
parent: 43
---
# Issue: Refinement dedup is silent

- **Area:** `pi-quest` persistence — `paths.ts:291-319`, `hooks/index.ts:284-292`
- **Runs observed:** `1788280759`
- **Severity:** Low — silent data loss, no log

`appendToFutureDraft` (`paths.ts:291`) returns `false` when `content.includes(slice(0,80))` for a refinement's leading 80 chars, without emitting any log. Caller `hooks/index.ts:284` only logs `DRAFT_APPENDED {hash,slice,draftPromptsCount}` when `appended===true`; the `false` path historically had no `DRAFT_APPEND_DEDUPED`. A second mention of the same prompt slice is therefore dropped with no audit trail. The `state.draftPrompts` array also applies a `PROMPT_MAX_COUNT` window that can hide earlier prompts.

Evidence: `execution.log` for `1788280759` shows `DRAFT_APPENDED` only, despite multiple `draftPrompts` pushes in `hooks/index.ts:287`.

Related: #01, #03, #08.
