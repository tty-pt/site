---
id: 11
title: "Agent can attempt a direct quest.md write before being told to use quest_update_state"
state: ready
severity: high
requires: []
validates: "direct cat > quest.md blocked with requires:quest_update_state"
area: "gates.ts: PROVISIONAL_RESEARCH_PENDING, tool_gating.ts, messaging.ts"
parent: 43
---
# Issue: Agent can attempt a direct `quest.md` write before being told to use `quest_update_state`

- **Area:** `pi-quest` gates/messaging — `gates.ts: PROVISIONAL_RESEARCH_PENDING`, `tool_gating.ts`, `messaging.ts`, `tools/update/executor.ts:65 syncQuestIdentity`
- **Runs observed:** `1788280759` at `93-96 16:41:05.461Z cat << 'EOF' > .pi/quest/current/consumer-side-complexity/quest.md`
- **Severity:** High — burns 3 failures before instruction

`gates.ts:PROVISIONAL_RESEARCH_PENDING` requires initial research and `quest_update_state` before implementation. However `tool_gating.ts: operationForTool(bash)` allows `bash` with `cat << 'EOF' > .pi/quest/current/**/quest.md` to be attempted; it is only blocked after the fact as `GATE_BLOCKED OPERATION_BLOCKED TOOL_ACTIVITY blocked consequence=OPERATION_BLOCKED failureId=block_mtiw96t1` with generic message `REQUIRED next step: Investigate ... and call quest_update_state`. The agent therefore invents the direct-write workaround and consumes `TOOL_FAILURE block_mtiw96t1` + `REASSESSMENT_REQUIRED round4 v3` + `REPEATED_FAILURE count=3` before the canonical `quest_update_state` succeeds at `16:41:21.579Z` (`gen1 592b9b73`). No proactive `reportAgentError`/`sendInternalAgentMessage` with stable `QuestErrorCode` and `requiredAction: use quest_update_state` is sent before the attempt.

Related: #10, #12, #16.
