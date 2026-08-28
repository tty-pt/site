# Quest: quest-journal-economy-subquest-sync

## Goal
Improve the `quest-journal` extension with:
1. High-fidelity persistent status bar representation of the current sub-quest and parent-child hierarchy.
2. A configurable token economy auto-compaction feature (defaulting to 140K tokens).
3. Pre-compaction exhaustive context preservation protocol so the agent updates quest files with extensive information from context before context is wiped, preventing any re-research in subsequent iterations.
4. Configurable early warning notification margin before compaction threshold (defaulting to 30k tokens before threshold, e.g. at 110k for a 140k limit) with explicit, high-visibility instructions to update quest files, immediately followed by auto-compaction upon updating (triggering around ~110k–130k+).
5. Autonomous sub-quest completion workflow: allow the agent to autonomously archive sub-quests and decide whether to auto-compact or continue based on context usage, without requiring unnecessary user modal interruption.

## Original request
> We want to improve the quest journal extension. First off, the persistent status bar doesn't represent the current subquest very well. And also, and most important. We want to implement an economy feature into this. We want the agent to auto-compact every X K tokens. (a configurable amount - default to 140K, for example). Before this compaction, we want to tell the agent to update the quest files with extensive information in the context, so that the new iteration does not re-research. Let's make a big quest for this update.

## Parent Quest
> If this is a sub-quest, reference the parent quest here (e.g. [[parent-quest-name]]).

## Current Status
- [x] done

## Build & Run Commands
> Commands to build, run, and test the project (discovered BEFORE modifying feature code).
- Build: N/A (TypeScript extension loaded via jiti at runtime by Pi)
- Run: `NODE_PATH="/home/quirinpa/.nvm/versions/node/v24.0.2/lib/node_modules/@earendil-works/pi-coding-agent/node_modules:/home/quirinpa/.nvm/versions/node/v24.0.2/lib/node_modules" npx tsx tests/unit/quest_journal_prompt_test.ts`
- Test: `NODE_PATH="/home/quirinpa/.nvm/versions/node/v24.0.2/lib/node_modules/@earendil-works/pi-coding-agent/node_modules:/home/quirinpa/.nvm/versions/node/v24.0.2/lib/node_modules" npx tsx tests/unit/<test-file>.ts`

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to test and execute extension code and mock Pi contexts.
- [x] **2. Write Tests First**: Developed unit test suite covering subquest hierarchy status bar formatting, economy threshold configuration, configurable 30k pre-compaction warning margin, auto-compaction on save, and autonomous subquest completion rules BEFORE implementation.
- [x] **3. Feature Implementation**: Developed enhanced status bar, economy token tracker, configurable warning margin, auto-compaction on save (via `quest_mark_saved`, `tool_result`, and `turn_end`), autonomous subquest completion rules, and configuration commands (`/quest-economy`, `/quest-warning`).
- [x] **4. Build & Run**: Built and executed test suites with zero syntax or runtime errors.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [x] **6. Server Restart & Full Test Suite**: Executed FULL test suite with zero errors.

## In-Depth Analysis & Findings
> Root cause analysis, architectural friction, abstraction opportunities.
1. **Status Bar Hierarchy Representation**:
   - `updateUIStatus` in `.pi/extensions/quest-journal.ts` uses `formatQuestHierarchy(state.active, state.stack)` to render clean breadcrumbs (`parent ↳ subquest` or `root ↳ mid ↳ leaf`).
   - Added token economy indicator `[tokens/threshold]` (e.g. `[45k/140k]`) and save state tags `(fresh)` / `(save pending)` / `(compaction ready)`.
   - Updated custom entry renderer in session trees to also render the full subquest hierarchy.

2. **Token Economy Auto-Compaction Feature**:
   - Configurable economy threshold `getEconomyThreshold()` with a default of 140,000 tokens (140k).
   - Priority hierarchy: in-session runtime setting (`state.economyTokens`) -> environment variable (`PI_QUEST_AUTO_COMPACT_TOKENS` / `QUEST_AUTO_COMPACT_TOKENS`) -> `settings.json` -> default (140k).
   - Command `/quest-economy [threshold] [warning]` to inspect or adjust thresholds (e.g. `/quest-economy 140k 30k`, `/quest-economy off`, `/quest-economy default`).

3. **Configurable Pre-Compaction Warning Window & Auto-Compaction Trigger**:
   - Configurable warning margin (`getWarningMargin()`, default 30,000 / 30k tokens) configurable via `PI_QUEST_PRE_COMPACT_WARNING_TOKENS`, `settings.json`, `/quest-economy <threshold> <warning>`, and `/quest-warning <margin>`.
   - Starting `warningMargin` tokens before threshold (e.g. 110k for a 140k limit with 30k margin), if save is pending, `sendDeepSaveRequest()` issues an urgent alert that auto-compaction is imminent and requires updating the quest file with an exhaustive context dump.
   - When the model saves the quest file via `quest_mark_saved` (or `tool_result` on the quest file) in this warning window, auto-compaction immediately triggers (`ctx.compact(...)`), cleanly executing around ~120k–130k tokens.
   - Compaction gate (`session_before_compact`) strictly blocks compaction unless a fresh save exists on disk since the last compaction.

4. **Autonomous Sub-Quest Completion Flow**:
   - Sub-quests complete autonomously without prompting the user with modal dialogs: the agent archives the sub-quest via `quest_archive({ compact: boolean })` (choosing `compact: true` if context is elevated or `compact: false` if context is fresh) and seamlessly resumes execution on the parent quest.
   - Root / top-level quest completion prompts the user via `ask_questions` with standard wrap-up options.

## Detailed Multi-Stage Execution Plan
> Each stage must be self-contained as if it were a single quest, with exact signatures, touched files, and targeted tests.

### Stage 1: Subquest Hierarchy Formatting & Enhanced Status Bar (PASS)
### Stage 2: Configurable Economy Settings & Token Tracking (PASS)
### Stage 3: Pre-Compaction Deep Context Preservation & Auto-Compaction Trigger (PASS)
### Stage 5: 30k Pre-Compaction Warning Window & Immediate Compaction on Save (PASS)
### Stage 6: Configurable Warning Margin (30k default) & Command Support (PASS)
### Stage 7: Autonomous Sub-Quest Completion & Context-Driven Compaction (PASS)

## Acceptance Criteria & Polish Checklist
- [x] Status bar displays clear subquest hierarchy (`parent ↳ subquest` or `root ↳ mid ↳ leaf`).
- [x] Status bar displays token usage relative to economy threshold (e.g. `[112k/140k]`).
- [x] Economy threshold is configurable with default `140k` tokens.
- [x] Pre-compaction warning margin is configurable with default `30k` tokens.
- [x] `/quest-economy` and `/quest-warning` commands allow viewing and configuring both economy threshold and warning margin.
- [x] Sub-quest completion is autonomous: agent does not need user input modal, and decides whether to compact based on available context before resuming parent quest.
- [x] Top-level quest completion prompts user with wrap-up options.
- [x] Compaction gate blocks premature compaction and ensures fresh quest file state.
- [x] All unit tests pass with zero errors.

## Sub-Quests
> Sub-quests, follow-ups, or tangent quests spawned from this quest.
- [ ] 

## Quest Refinements & User Feedback Loops
> Mid-workflow refinements, post-implementation iterations, and user adjustments.
- Initial user request: Add subquest representation to persistent status bar, implement configurable auto-compaction economy feature (default 140k), and enforce deep pre-compaction context preservation so iterations do not re-research.
- Refinement 1: Make it very clear in the extension ~30K before compaction to the agent that a compaction will happen soon, and instruct it to update the quest files.
- Refinement 2: After updating the quest files, compaction triggers immediately.
- Refinement 3: Make the 30K warning value configurable with 30K default (140k economy with 30k pre-compaction triggers around ~130k on save).
- Refinement 4: Sub-quest completion does not require user input; agent autonomously decides whether to compact or continue based on available context and resumes parent quest seamlessly.

## Why this matters
> Context, motivation, stakeholders.
Allowing the agent to autonomously complete sub-quests and intelligently choose when to compact keeps development velocity high without constantly interrupting the user for intermediate sub-task transitions.

## Decisions made
- Differentiate top-level quest completion (prompts user via `ask_questions`) from sub-quest completion (autonomous archive + context-driven compaction decision + automatic parent resumption).
- Sub-quest archiving can set `compact: true` when context is high or `compact: false` when context is fresh.
- `DEFAULT_PRE_COMPACT_WARNING_TOKENS = 30_000` (30k default).
- Support configuring warning margin via `PI_QUEST_PRE_COMPACT_WARNING_TOKENS`, `settings.json`, `/quest-economy <threshold> [warning]`, and `/quest-warning <margin>`.

## Constraints & Rules
- Isomorphic / clean TypeScript extension code without breaking existing Pi hooks.
- Zero external runtime dependencies outside standard node / pi SDK.
- Non-blocking async execution.

## Files touched
- `.pi/extensions/quest-journal.ts`
- `tests/unit/quest_journal_prompt_test.ts`
- `tests/unit/quest_journal_subquest_test.ts`
- `tests/unit/quest_journal_status_test.ts`
- `tests/unit/quest_journal_economy_test.ts`
- `tests/unit/quest_journal_compaction_economy_test.ts`
- `docs/current/quest-journal-economy-subquest-sync.md`

## Remaining work
- [x] All implementation and refinement stages verified with passing tests.

## Open questions / risks
- None.

## Next recommended step
1. Complete quest wrap-up and prompt user for next action (refine, archive & auto-compact, archive without auto-compact, or manual mode).

## Resume prompt
> Resuming quest-journal-economy-subquest-sync. All features and refinements have been implemented and verified with full test coverage. The extension now supports subquest breadcrumb hierarchy in the persistent status bar, configurable 140k token auto-compaction economy, configurable 30k pre-compaction warning, immediate auto-compaction upon quest file update, and /quest-economy & /quest-warning commands.
