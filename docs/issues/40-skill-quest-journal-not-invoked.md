---
id: 40
title: "Skill quest_journal not invoked — agent runs without durable quest establishment"
state: done
severity: high
requires: []
validates: "pi: initial prompt => DIALOGUE + SEMANTIC_SNAPSHOT + skill hint steer within turn0"
area: "hooks/index.ts:546 registerQuestJournalCRBHook, markdown.ts getFullWorkflowInstructions/getCompactWorkflowInstructions, "
parent: 43
---
# Issue: Skill `quest_journal` not invoked — agent runs without durable quest establishment

- **Area:** `pi-quest` skill/workflow — `hooks/index.ts:546 registerQuestJournalCRBHook`, `markdown.ts getFullWorkflowInstructions/getCompactWorkflowInstructions`, `context.ts buildSessionAwarenessBlock`, `messaging.ts shouldCapturePrompt`, `.pi/skills/quest-journal/SKILL.md`
- **Runs observed:** `1788299416` 21:50 `01a05ef3` `look-consumer-side-code-lot-complexity` — 21 `TURN_START` `phase research gate RESEARCH_PENDING plan v1 round 1` with 10 `RESEARCH_EVIDENCE` `read future/...md` `bash ls mods/ external/bud/hyle`, never called `quest_update_state` to establish `current/1788299416/quest.md` (`RESUME_STATE_INCONSISTENT ENOENT` + `SAVE_FAILED file_not_found` 17×). No `Skill: quest_journal` trigger in `before_agent_start` systemPrompt, `TURN_START intent="Look at the consumer side code..."` repeated without CRB `quest_journal` tools hint. Same pattern `1788298649` and `1788280759`.
- **Severity:** High — without skill, durable `quest.md` never created, `NO_PROGRESS turns 5→15` + `periodic_checkpoint` steer loops, `PROVISIONAL_RESEARCH_PENDING` blocks `bash cat << 'EOF' > quest.md` as `TOOL_FAILURE` → `REASSESSMENT_REQUIRED round2→6`.

## Current behavior

- `installWorkflowSystemPrompt before_agent_start` injects `getFullWorkflowInstructions(resumeContext)` + `buildSessionAwarenessBlock` but without explicit `Skill: quest_journal` trigger phrase. Pi's tool router therefore treats initial prompt as generic research, runs `bash ls` evidence loop, never calls `quest_update_state { goal, originalRequest, plan }` via skill.
- `registerQuestJournalCRBHook` adds `COMPACT_WORKFLOW_RULES` to CRB providers, but only when `set.has(quest_mark_saved)` etc. — not on fresh draft with `activeDraft` only.

## Desired behavior

- `before_agent_start` systemPrompt must prepend `Skill: quest_journal` when `!state.active && (state.pendingRootQuest || state.activeDraft)` so model invokes `quest_journal` skill on turn 0. Minimal: add `Skill trigger: quest_journal` line to `buildSessionAwarenessBlock` or to workflow instructions header (template, no LLM). No token spend beyond prompt template.
- Keep `DIALOGUE dialogueRole=user slice 200 piSessionId` + `SEMANTIC_SNAPSHOT phase research gate RESEARCH_PENDING` always-on (done) so `execution.log` shows skill hint steer `AGENT_MESSAGE_DELIVERED` within turn 0.

## Manual validation in `pi`

1. Start `pi`, send initial `Look at consumer side...` → `execution.log` must show `DIALOGUE user 200-char + hash` then `SEMANTIC_SNAPSHOT research:1:RESEARCH_PENDING — phase research...` then `AGENT_MESSAGE_DELIVERED steer Skill: quest_journal` within `TURN_START 0`.
2. Next `TURN_START 1` should attempt `quest_update_state` with research findings, not 5 `bash ls` loops.

Related: #01, #06, #10, #41, #42, #36.
