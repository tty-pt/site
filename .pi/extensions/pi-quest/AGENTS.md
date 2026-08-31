# AGENTS.md — pi-quest package guidelines

`pi-quest` is the official Quest Journal & Epistemic Workflow Extension for the Pi Coding Agent harness.

## Architecture & Subsystems
The package is located in `.pi/extensions/pi-quest/`:
- `index.ts` — Package entry point re-exporting from `./src/index.ts`.
- `src/` — Modular subsystems divided across 26 architectural sections:
  1. `types.ts` — Host API types, state models, lifecycle and permission enums
  2. `constants.ts` — Path definitions, token thresholds, aliases, prefixes
  3. `utils.ts` — String, token arithmetic, path normalization, and hash helpers
  4. `paths.ts` — Quest path builders, slug generators, filesystem checks
  5. `markdown.ts` — Section/block parsers, splicers, and templates
  6. `state.ts` — Session states, proxy handler, state reconstruction
  7. `context.ts` — Session awareness, token calculations, git and standing notes
  8. `reconstruction.ts` — State and intent reconstruction from markdown journals
  9. `validation.ts` — Research prerequisites validation and consistency auditing
  10. `research.ts` — Reassessment triggers and research prompts
  11. `gates.ts` — Implementation gate policies (`canImplement`, block reasons)
  12. `messaging.ts` — Model messaging, prompt builders, save requests
  13. `tool_gating.ts` — Tool call and bash command classifiers, permission gates
  14. `persistence.ts` — Session persistence, save verification, and mark-saved logic
  15. `compaction.ts` — Compaction pressure calculations, pre-compact checkpoints, resume directives
  16. `subquest.ts` — Sub-quest parent/child linking, completion, and child returns
  17. `lifecycle.ts` — Quest creation, activation, archival, and interactive choice prompting
  18. `classification.ts` — User message classification and confirmation handling
  19. `ui.ts` — Status bar rendering
  20. `hooks.ts` — Pi lifecycle event listeners (`turn_end`, `session_before_compact`, `session_compact`, etc.)
  21. `tools.ts` — Tool registrations (`quest_update_state`, `quest_mark_saved`, `quest_subquest`, `quest_archive`)
  22. `commands.ts` — Slash command registrations (`/quest`, `/subquest`, `/future`, `/archive`, etc.)
  23. `index.ts` — Subsystem aggregator, export surface, and default extension entry point
- `tests/` — Test suites for all pi-quest lifecycle and gate behaviors.

## Architectural Invariants
1. **Uniform Agent-Visible Communication**: Any extension event that changes what the agent is permitted or required to do (enforcement blocks, gate rejections, persistence errors, compaction/continuation directives) MUST produce a model-visible message via `reportAgentError` / `sendInternalAgentMessage` describing the state change, the stable `QuestErrorCode`, and the required next action. UI notifications and diagnostic logs are never considered sufficient model feedback.
2. **Strict Acyclic Architecture**: Source modules in `src/` must form a strict directed acyclic graph (DAG) without circular imports.
3. **Pure-C Boundary**: No JavaScript in project application paths.

## Verification & Testing
Run the complete unit test suite from the package directory or project root:
```bash
deno test --allow-all .pi/extensions/pi-quest/tests/
```
or via npm:
```bash
npm --prefix .pi/extensions/pi-quest test
```
All 40 test suites (178 steps) must pass with zero failures.

## Packaging Requirement (CRITICAL)
Whenever you finish a task on `pi-quest` or test the agent, **generate the unified bundle ZIP archive**:
```bash
cd .pi/extensions/pi-quest && npm run zip
```
or from the repository root:
```bash
npm --prefix .pi/extensions/pi-quest run zip
```
Running `npm run zip` creates `pi-quest-bundle.zip` at the project root containing:
- `pi-quest/` — Current extension source tree and package
- `diagnostic/current-run/` — Run manifest (`manifest.txt`), root quest (`quest/`), sub-quests (`subquests/`), and execution log (`run.log`).

The packaging command prints the exact bundle path, active Run ID, Root Quest, and archive SHA-256 hash (also recorded in `manifest.txt`), and the active Run ID is visible in the Quest Journal UI/status whenever a quest is active.

Send `pi-quest-bundle.zip` for post-run evaluation or extension distribution.
