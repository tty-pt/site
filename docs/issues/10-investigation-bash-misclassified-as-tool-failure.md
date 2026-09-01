---
id: 10
title: "Investigation bash commands are misclassified as TOOL_FAILURE and trigger reassessment"
state: ready
severity: high
requires: []
validates: "wc -l/fd not TOOL_FAILURE when investigation"
area: "tool_gating.ts, utils/shell_parser.ts, validation.ts"
parent: 43
---
# Issue: Investigation `bash` commands are misclassified as `TOOL_FAILURE` and trigger reassessment

- **Area:** `pi-quest` gating/validation — `tool_gating.ts`, `utils/shell_parser.ts`, `validation.ts`, `research.ts`, `utils/tool_permissions.ts`
- **Runs observed:** `1788280759` at `30 fail_3 16:39:46.244Z wc -l external/bud/src/*.c ... 2>/dev/null`, `36 fail_4 ls external/bud/include/ && wc -l ... && fd ...`, `258 wc -l /home/quirinpa/site/external/hyle/c/libhyle-bud/*.c` and `wc -l /home/quirinpa/site/external/bud/bud.c`; also `1788299416` `turn7 gate_mtj7bdso` `turn0 gate_mtj7ctf2` `turn5 gate_mtj7dsbn` `bash cat << 'EOF' > .pi/quest/current/1788299416/quest.md` `GATE_BLOCKED PROVISIONAL_RESEARCH_PENDING RESEARCH_REQUIRED` misrouted as `TOOL_FAILURE` → `REASSESSMENT_REQUIRED round2→6 version1→5` (see #41)
- **Severity:** High — spurious `REASSESSMENT_REQUIRED round 2→6` and `NO_PROGRESS`

Investigation commands such as `wc -l external/bud/src/*.c external/bud/src/*.h ... 2>/dev/null && echo --- && ls ...` and `fd -H -t f ...` are classified as failures when the glob has zero matches or `fd` is missing (bash `nullglob` off, non-zero exit propagates through `&&`). The gating layer records `TOOL_FAILURE {command, reason: bash command} category=TOOL_FAILURE` and `research.ts` triggers `REASSESSMENT_REQUIRED GATE_BLOCKED_REASSESSMENT_PENDING` (`32 round2`, `38 round3`, `159 round5`, `264 round6`). Expected behavior per `docs/CONVENTIONS.md` and `EXPANDED §3.1` is that evidence-gathering `rg/wc/fd/ls` should be `|| true` tolerant and not count toward failure thresholds. `bash > quest.md` gate blocks are a separate cause (#41).

Related: #09, #11, #12, #41.
