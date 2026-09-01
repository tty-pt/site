---
id: 17
title: "Allowlisted DAG cycles and project-root discovery pollute the extension"
state: ready
severity: low
requires: []
validates: "check-pi-quest-dag passed, findProjectRoot skips extensions/"
area: "constants.ts, diagnostic/hierarchy/project.ts:4 findProjectRoot, diagnostic/packaging.ts:230"
parent: 45
---
# Issue: Allowlisted DAG cycles and project-root discovery pollute the extension

- **Area:** `pi-quest` architecture — `constants.ts`, `diagnostic/hierarchy/project.ts:4 findProjectRoot`, `diagnostic/packaging.ts:230`, `scripts/check-pi-quest-dag.ts`, `AGENTS.md §2`
- **Runs observed:** `1788280759` bundle `Verification: PASSED` with mirror ` .pi/extensions/pi-quest/.pi` (`5+2` entries) on disk `2026-09-01`
- **Severity:** Low — maintenance burden, test-fixture leak

`AGENTS.md §2` requires a strict DAG. Two cycles are allowlisted (`check-pi-quest-dag.ts:109`): `messaging↔persistence` (`logging/types.ts` leaf issue) and `paths↔markdown↔markdown_parse`. New leaf additions such as `messaging.ts → logging.ts` static import and `reconstruction.ts → constants.ts FUTURE_DIR` must stay inside those cycles; any new `paths↔state` or `config↔state` edge would be a violation but the allowlist hides drift.

`diagnostic/hierarchy/project.ts:4 findProjectRoot` walks ancestors looking for `package.json`/`deno.json` but does not skip the `extensions/` prefix, so ` .pi/extensions/pi-quest/.pi/quest/current/1788271193/...` mirror (test run `5` entries, moved to `tests/.pi-fixture` in plan) is discovered as a project root and can pollute `collectQuestInfos` and `discoverRunLogs`. `diagnostic/packaging.ts:230` already skips `.pi` when zipping, but hierarchy does not.

Related: #05, #07.
