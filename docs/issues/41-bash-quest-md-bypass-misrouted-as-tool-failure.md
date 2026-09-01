---
id: 41
title: "Bash cat > quest.md bypass misrouted as TOOL_FAILURE and triggers reassessment"
state: ready
severity: high
requires: [10]
validates: "bash cat > quest.md while RESEARCH_PENDING => GATE_BLOCKED not TOOL_FAILURE, no REASSESSMENT bump"
area: "tool_gating.ts:222, validation.ts, research.ts triggerReassessment"
parent: 43
---
# Issue: `bash cat > quest.md` bypass misrouted as `TOOL_FAILURE` and triggers reassessment

- **Area:** `pi-quest` gating — `tool_gating.ts:222`, `validation.ts`, `research.ts triggerReassessment`, `hooks/handlers.ts handleToolResult/detectBashToolFailure`, `gates.ts canImplement`, `constants.ts QuestErrorCode`
- **Runs observed:** `1788299416` `turn7 gate_mtj7bdso` `turn0 gate_mtj7ctf2` `turn5 gate_mtj7dsbn` — `tool bash blocked` `command="cat << 'EOF' > .pi/quest/current/1788299416/quest.md # Quest: consumer-side-complexity ..."` `GATE_BLOCKED PROVISIONAL_RESEARCH_PENDING RESEARCH_REQUIRED` → `TOOL_FAILURE category=TOOL_FAILURE` → `REASSESSMENT_REQUIRED round2→6 version1→5` `NO_PROGRESS`. Extends `10` (investigation bash `wc -l/fd`) and `11` (direct quest.md write) but distinct: gate-blocked `bash` with `quest.md` path should be `GATE_BLOCKED` opening, not failure-counted.
- **Severity:** High — spurious `REASSESSMENT_REQUIRED` flood and `NO_PROGRESS` loop prevents durable establishment even when `DIALOGUE`+`SEMANTIC_SNAPSHOT` are correct.

## Current behavior

- `tool_gating.ts isQuestUpdateTool / isPathToActiveQuest` classifies `write/edit` tools, but `bash` with `> quest.md` passes through `classifyToolCall` as `bash` then `tool_gating` `GATE_BLOCKED` `PROVISIONAL_RESEARCH_PENDING`, yet `handlers.ts handleToolResult` records `effectiveIsError = rawIsError` then `TOOL_FAILURE` + `failureId block_...` → `research.ts` increments `reassessmentVersion`.
- Whitelist in `handlers.ts:266` `detectBashToolFailure` only whitelists `rg` exit 1 no-match, not `cat > quest.md` gate block.

## Desired behavior

- `bash` commands that target `QUEST_CURRENT_DIR/<qid>/quest.md` (or `FUTURE_DIR`) and are blocked by `PROVISIONAL_RESEARCH_PENDING` must be recorded as `GATE_BLOCKED` `code=RESEARCH_REQUIRED` not `TOOL_FAILURE`; `consequence=OPERATION_BLOCKED` not `FAILURE_RECORDED`, no `reassessmentVersion` bump. Steer should say `quest_update_state required` not `Command failed`.
- Minimal: in `handleToolResult`/`detectBashToolFailure`, if `command` contains `QUEST_CURRENT_DIR` or `quest.md` and `gate==PROVISIONAL_RESEARCH_PENDING`, set `effectiveIsError=false` and `consequence=GATE_BLOCKED`, skip `REASSESSMENT_REQUIRED`.

## Manual validation in `pi`

1. Repeat `1788299416` initial prompt → `TURN_START 7` `bash cat << 'EOF' > current/.../quest.md` while `RESEARCH_PENDING` must show `GATE_BLOCKED` `IMPLEMENTATION_BLOCKED` `TOOL_ACTIVITY bash operation=blocked` but no `TOOL_FAILURE` nor `REASSESSMENT_REQUIRED` increment.
2. `python docs/issues/next.py` after `10 done` → `41` becomes `ready`.

Related: #09, #10, #11, #12, #40, #42.
