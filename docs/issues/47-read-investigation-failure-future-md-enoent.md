---
id: 47
title: "Read investigation failure future md ENOENT misclassified as FAILURE_RECORDED"
state: done
severity: high
requires: []
validates: "read .pi/quest/future/...md ENOENT must not be FAILURE_RECORDED"
area: "hooks/turn_analysis.ts:98-143 + hooks/handlers.ts + execution.log:34-36"
parent: 43
---
# Issue: Read investigation failure future md ENOENT misclassified as FAILURE_RECORDED

- **Area:** `pi-quest` gates — `hooks/turn_analysis.ts:98-143 detectBashToolFailure` (only whitelists bash `rg/wc/ls/fd` exit 1) + `hooks/handlers.ts` tool_result classification + `execution.log:34-36` `1788305314`
- **Runs observed:** `1788305314` `turn0` 3× `TOOL_ACTIVITY read failure path=.pi/quest/future/look-consumer-side-code-lot-complexity.md` + `external/bud/libbud.c` + `external/bud/bud.h` `reason="tool execution error"` `operation=FAILURE_RECORDED` while provisional `RESEARCH_PENDING`. These are discovery probes (`file-read` `RESEARCH_EVIDENCE` at `10` for `bash ls .pi/quest/future/` succeeded but `read` failed) — should be ignored/whitelisted like investigation bash, not `unrecoveredFailures` chain `recoveryFor=fail_1_mtjatjlg_8kjn`.
- **Severity:** High — triggers `PLAN_REVIEW_UNCERTAIN:missing evidence` at `24` spuriously.

Related: #10 (bash wc/fd done), #41 (cat>quest.md done), #52 (slug mismatch).
