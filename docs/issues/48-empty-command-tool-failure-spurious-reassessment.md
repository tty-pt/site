---
id: 48
title: "Empty-command TOOL_FAILURE triggers spurious REASSESSMENT"
state: ready
severity: high
requires: [47]
validates: "TOOL_FAILURE command=\"\" must not trigger REASSESSMENT_REQUIRED"
area: "hooks/turn_analysis.ts:103-123 + execution.log:80-82"
parent: 43
---
# Issue: Empty-command TOOL_FAILURE triggers spurious REASSESSMENT

- **Area:** `pi-quest` gates — `hooks/turn_analysis.ts:103-123` (`cmd = tr.args.command||...||""` then `if(toolFailed) return hasFailure:true` with empty cmd) + `execution.log:80-82` `1788305314`
- **Runs observed:** `1788305314` `turn5` 3× `bash operation=success` (51,57,68-69,75,77,79) then `80 TOOL_FAILURE quest=look-… turn=5 consequence=TOOL_ERROR failureId=fail_1_mtjatjlg_8kjn command= reason="Command failed with error: bash command"` → `81 REASSESSMENT_REQUIRED GATE_BLOCKED_REASSESSMENT_PENDING` → `82 TURN_END REASSESSMENT_PENDING` bumps `round 1→2` spuriously. Empty `command=""` not in whitelist.
- **Severity:** High — spurious reassessment blocks `quest_update_state`.

Related: #10, #41, #47.
