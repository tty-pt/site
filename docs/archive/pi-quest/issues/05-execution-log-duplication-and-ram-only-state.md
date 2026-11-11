---
id: 05
title: "Execution log is duplicated and its location state is RAM-only"
state: done
severity: medium
requires: []
validates: "runs/ empty + finalized_logs deleted on archive, current first"
area: "logging/paths.ts:19-55, logging/core.ts:27, lifecycle/archive/removal.ts:9-14"
parent: 44
---
# Issue: Execution log is duplicated and its location state is RAM-only

- **Area:** `pi-quest` logging — `logging/paths.ts:19-55`, `logging/core.ts:27`, `lifecycle/archive/removal.ts:9-14`, `state.ts:6-12`, `context.ts:7`
- **Runs observed:** `1788280759` (`current/1788280759/execution.log` + `finalized_logs/` on HDD 209 files)
- **Severity:** Medium — wasted blocks + `RESUME_STATE_INCONSISTENT`

`logging/core.ts:27 appendFileSync` writes to `current/<qid>/execution.log` (live run dir, also bundle staging `diagnostic/packaging.ts:112 runDir`). `lifecycle/archive/removal.ts:10 pinLogToFinalized` duplicates that content to `finalized_logs/<qid>.log` (`mkdir finalizedLogDir` + `writeFile pinnedLogPath`). On `2026-09-01` `read .pi/quest` showed 209 files in `finalized_logs/` and an empty `runs/` dir.

`logging/paths.ts:7 pinnedLogPaths Map` and `state.ts:6 sessionStates, 7 sessionStartMap, 10 asyncContext, 12 lastGeneratedSec` plus `context.ts:7 cachedGuidelinesFingerprint` are RAM-only. A draft-only qid such as `1788278057` therefore has `current/1788278057/execution.log` but no `quest.md` → `RESUME_STATE_INCONSISTENT ENOENT .pi/quest/current/1788278057/quest.md` at `1788280759:06`.

Related: #06, #07.
