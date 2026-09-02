---
id: 58
title: "Subagent reads extension source instead of site — wrong working directory / search path on QUEST_REUSED mount"
state: done
severity: medium
requires: [56]
validates: "QUEST_REUSED subagent reads only repo-root site files, never .pi/extensions/pi-quest/src, and does not emit ENOENT/fd thrash"
area: "hooks/index.ts, paths.ts, critical_agent/tracker.ts, critical_agent/policy/launch_guard.ts"
parent: 44
---
# Issue: Subagent reads extension source instead of site

- **Area:** `pi-quest` subagent / path resolution — `hooks/index.ts` (session mount / initial prompt), `paths.ts` (questPath / repo root), `critical_agent/tracker.ts` (`canLaunchReview` at `:122`), `critical_agent/policy/launch_guard.ts` (`:4,16`), `critical_agent/policy/background.ts`, `execution.log` research paths.
- **Runs observed:** `1788349108` (2026-09-02 11:38–11:45) — two extra sessions `01a061ed-9f79-71e3-a43d-a0cfb2cae5ff` (118 lines) and `01a061ed-9daf-77cf-bf96-27e63487c9f5` (120) did `QUEST_REUSED` (`execution.log:231,238`) on the same questId, then their first `RESEARCH_EVIDENCE` / tool reads targeted the **extension's own source**:
  ```
  /home/quirinpa/.pi/extensions/pi-quest/src/critical_agent/tracker.ts        (×3)
  /home/quirinpa/.pi/extensions/pi-quest/src/critical_agent/policy/launch_guard.ts (×2)
  /home/quirinpa/.pi/extensions/pi-quest/src/critical_agent/policy/background.ts
  /home/quirinpa/.pi/extensions/pi-quest/src/critical_agent/prompt/build.ts
  /home/quirinpa/.pi/extensions/pi-quest/src/hooks/index.ts                   (×2)
  ```
  The site the quest targets was touched only once (`mods/index/ux/list_json.c`, `01a061ed-9f79 turn7 :242`). Errors from the wrong dir:
  - `ENOENT: no such file or directory, access '…/pi-quest/src/critical_agent/policy/launch_guard.ts'` (×2, thought `1ff1457050d0`)
  - `Path not found: /home/quirinpa/.pi/extensions/pi-quest/src/critical_agent/policy`
  - `[fd error]: Search path '/home/quirinpa/.pi/extensions/pi-quest/src' is not a directory. No valid search paths given.` (thought `39e92b3a9c37`)
  - `not found no realpath`
  Stray sessions then looped: 5× `NO_PROGRESS` (`:411 turns=5`, `:421 turns=6`, `:434 turns=6`, `:460 turns=7`, `:480 turns=7`) + 5× `REVIEW_DEDUP_HIT … reviewKind=direction reason=not_registered` (`:412,422,435,461,481`) + 5× `CRITICAL_REVIEW_SUPPRESSED_DUPLICATE … reviewId=not_registered` (`:413,423,436,462,482`).
- **Severity:** Medium — wastes turns, pollutes the log with extension-source thrash, triggers `NO_PROGRESS` / dedup noise; root cause is #56 (stray mount allowed), this is the wrong-dir symptom.

## Current behavior

- When a stray `QUEST_REUSED` session mounts (see #56), it receives the same initial prompt ("Look at the consumer side code…") but resolves research paths against a wrong base (likely the extension directory or an unqualified relative search path). `canLaunchReview` / `launch_guard.ts` / `tracker.ts` etc. are extension internals, not site code — the site's consumer side is `mods/*/ux`, `mods/common/ux`, `external/hyle/c/libhyle-bud`, `external/bud`, etc. (as the main session `01a061e9-…` correctly read 28 site files).
- The failing reads still count as tool activity but produce no `RESEARCH_EVIDENCE` with a valid site target, so `NO_PROGRESS` fires (`turns without state checkpoint`) and the direction-review dedup path is entered with `not_registered`.

## Desired behavior

- Validate the subagent / `QUEST_REUSED` session's working directory and search roots against the quest's repository root (`paths.ts:questPath` / `repoPath`). Fail fast with a single `INVALID_SEARCH_ROOT` diagnostic instead of ENOENT/fd thrash.
- Ensure any session mounted via `QUEST_REUSED` inherits the original quest's repo root / quest dir, and any `code-search` / `read` tool that targets `.pi/extensions/pi-quest/src` for a site quest is rejected or warned (that path is extension internals, not site consumer code).
- Once #56 prevents stray mounts, this symptom disappears; keep this issue as the "wrong-dir" guard in case a stray mount still occurs.

## Manual validation in `pi`

1. Trigger the `1788349108`-style second `QUEST_REUSED` mount while the main session is `REASSESSMENT_PENDING`. Before fix: new session reads `…/pi-quest/src/critical_agent/*` and logs ENOENT. After fix (via #56): mount is refused/coalesced and no stray session starts. If a stray still starts, a single `INVALID_SEARCH_ROOT` is logged instead of 5+ ENOENTs and no `NO_PROGRESS` loop occurs.

Related: #16, #37, #56.
