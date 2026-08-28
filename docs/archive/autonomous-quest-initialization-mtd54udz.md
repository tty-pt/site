# Quest: autonomous-quest-initialization

## Goal
Eliminate initial popup questions / blocking selector on boot in the quest journal extension. The user should be able to type normally at the prompt. The agent will autonomously manage quests:
1. When the user describes an issue or feature to work on, the agent automatically conducts research, brainstorms, creates the quest file, and activates it.
2. If the user explicitly mentions continuing or resuming an existing quest without naming it, the agent prompts to ask which quest to continue.

## Original request
> The quest journal extension should not ask questions to the user initially. The user should type as usual, and then the agent should know to use the quest system to start working. If the user mentions continuing a quest, the agent should then ask which one. If the user describes an issue they want fixed, it should automatically engage on research and brainstorm for this new quest, creating it and activating it as appropriate.

## Current Status
- [x] done

## Build & Run Commands
- Build: `make`
- Run: `make watch` or `./start.sh`
- Test: `NODE_PATH=/home/quirinpa/.nvm/versions/node/v24.0.2/lib/node_modules/@earendil-works/pi-coding-agent/node_modules npx tsx tests/unit/quest_journal_prompt_test.ts`

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to test extension lifecycle and prompt behavior.
- [x] **2. Write Tests First**: Developed `testQuestJournalSessionStartNoModal` in `tests/unit/quest_journal_prompt_test.ts` before finalizing changes.
- [x] **3. Feature Implementation**: Removed `offerQuestChoiceOnBoot` from `session_start` in `.pi/extensions/quest-journal.ts` and updated instructions.
- [x] **4. Build & Run**: Verified clean execution without UI modal popup interrupts.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [x] **6. Server Restart & Full Test Suite**: Verified unit tests and E2E suites.

## In-Depth Analysis & Findings
1. **Startup Modal Interruption (`.pi/extensions/quest-journal.ts`)**:
   - `pi.on("session_start", ...)` was calling `offerQuestChoiceOnBoot(...)`, which invoked `ctx.ui.select(...)` before the user could type their prompt.
   - Removed `offerQuestChoiceOnBoot` call so startup is clean and unblocked.
2. **Autonomous Agent Flow**:
   - Updated system prompt instructions injected by `installWorkflowSystemPrompt`:
     - Do not ask questions on startup; let the user type as usual.
     - When an issue or feature is described, automatically research, brainstorm, create the quest file on disk (`docs/current/<slug>.md`), and activate it.
     - If the user asks to continue or resume an existing quest without specifying which one, clarify using `ask_questions` with available quest list.

## Decisions made
- Removed `offerQuestChoiceOnBoot` from `session_start` hook in `.pi/extensions/quest-journal.ts`.
- Retained manual `/quest`, `/quests`, and `/subquest` commands for direct command line use.
- Added automated unit test `testQuestJournalSessionStartNoModal` in `tests/unit/quest_journal_prompt_test.ts`.

## Files touched
- `.pi/extensions/quest-journal.ts`
- `tests/unit/quest_journal_prompt_test.ts`
- `docs/current/autonomous-quest-initialization.md`

## Remaining work
- [x] None. All tests passing and requirements satisfied.

## Next recommended step
1. Choose an action via the prompt below: archive with compaction, archive without compaction, or continue refining.
