# Quest: sub-quests

## Goal
Add a sub-quest (and subsequent/child quest) architecture to the Pi quests extension (`.pi/extensions/quest-journal.ts`). When a user makes remarks mid-quest that are tangent, follow-up, or deserve dedicated tracking, a sub-quest is created as its own quest file (`docs/current/<sub-quest>.md`), linked bidirectionally with the parent quest, deep-researched with full TDD and quality gates, and integrated with `/subquest`, `/quests`, status widgets, custom tools, and prompt injection rules.

## Original request
> Let's add a sub-quest feature to our quests extension. So that if a user makes any remarks mid-quest, that might not be super direct, a sub-quest (or subsequent quest) is added. It needs to be deeply researched before considering done, just like another quest, it should be its own quest file, and be referenced by the original.

## Current Status
- [x] not started · in progress · blocked · done

## Build & Run Commands
> Commands to build, run, and test the project (discovered BEFORE modifying feature code).
- Test targeted unit tests: `NODE_PATH=/home/quirinpa/.nvm/versions/node/v24.0.2/lib/node_modules/@earendil-works/pi-coding-agent/node_modules:/home/quirinpa/.nvm/versions/node/v24.0.2/lib/node_modules npx tsx tests/unit/quest_journal_subquest_test.ts && NODE_PATH=/home/quirinpa/.nvm/versions/node/v24.0.2/lib/node_modules/@earendil-works/pi-coding-agent/node_modules:/home/quirinpa/.nvm/versions/node/v24.0.2/lib/node_modules npx tsx tests/unit/quest_journal_prompt_test.ts`

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build and run targeted TypeScript unit tests via `npx tsx`.
- [x] **2. Write Tests First**: Developed `tests/unit/quest_journal_subquest_test.ts` covering subquest creation, linking, tool execution, widget hierarchy, and prompt injection BEFORE implementing features.
- [x] **3. Feature Implementation**: Developed sub-quest feature in `.pi/extensions/quest-journal.ts` to satisfy all tests.
- [x] **4. Build & Run**: Verified TypeScript execution with zero errors.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [x] **6. Server Restart & Full Test Suite**: Verified full unit test suite passing with zero errors.

## In-Depth Analysis & Findings
1. **Parent-Child Quest Model**:
   - Primary quests live in `docs/current/<quest>.md`.
   - Sub-quests also live as independent first-class quest files in `docs/current/<sub-quest>.md` (or `docs/future/<sub-quest>.md` if deferred).
   - Bidirectional Linking:
     - Parent quest markdown has a `## Sub-Quests` section listing child quests with checkbox status (`- [ ] [[sub-quest-name]] — description`).
     - Sub-quest markdown has a `## Parent Quest` section referencing the parent (`[[parent-quest-name]]`).
   - Deep Research & Quality Gates: Sub-quests must follow identical TDD & Quality checklist requirements (discovery, tests first, implementation, clean code, verification) before being marked done.
2. **Commands & CLI UX**:
   - `/subquest [name] [goal/description...]` (aliases: `/sub-quest`, `/subtask`, `/task-subquest`):
     - If active quest exists: creates new sub-quest file, links it into active quest file's `## Sub-Quests` section, and sets active quest to the sub-quest.
     - If in TUI mode and name/goal missing, prompts interactively for name and goal.
   - `/quests` (alias: `/tasks`):
     - Hierarchically renders active quests with their associated sub-quests nested/indented under the parent (`↳ <child>`).
   - `/quest-status` (alias: `/task-status`):
     - Displays parent quest link if current active quest is a sub-quest, plus list of sub-quests if current quest has any.
3. **Custom Tool Support**:
   - Registered `quest_journal_subquest` (alias: `task_journal_subquest`) tool so the model can programmatically spawn and link sub-quests when processing conversational remarks.
4. **Prompt & Protocol Enhancement**:
   - System prompt instructions injected via `before_agent_start` instruct the agent how to handle mid-quest remarks, suggestions, and tangential requirements by spawning sub-quests, linking them in the parent file, and ensuring deep research before declaring done.
   - Active Quest Resume Context extraction includes `## Parent Quest` and `## Sub-Quests` sections.

## Detailed Multi-Stage Execution Plan
### Stage 1: Template & Parser Upgrades
- **Target**: Update `QUEST_TEMPLATE`, `FUTURE_QUEST_TEMPLATE`, and resume context parser in `.pi/extensions/quest-journal.ts` to support `## Parent Quest` and `## Sub-Quests`.
- **Tasks**:
  - Added `## Parent Quest` and `## Sub-Quests` sections to `QUEST_TEMPLATE`.
  - Added helper functions `linkSubQuestInParent(parentSlug, childSlug, description)`, `extractParentFromQuest(content)`, and `extractSubQuestsFromQuest(content)`.
  - Updated `loadActiveQuestResumeContext` to extract `Parent Quest` and `Sub-Quests`.
- **Targeted Tests**: `tests/unit/quest_journal_subquest_test.ts`.

### Stage 2: Commands & Interactive Handlers (`/subquest`, `/quests`, `/quest-status`)
- **Target**: Implement `/subquest` command and hierarchical display in `/quests` and `/quest-status`.
- **Tasks**:
  - Implemented `subquestHandler` with interactive prompts, slugification, file creation, parent linking, and session activation.
  - Updated `questsHandler` to parse parent-child relationships and display nested tree in TUI widget (`↳ <subquest>`).
  - Updated `questStatusHandler` to show parent and child quest links.
- **Targeted Tests**: `tests/unit/quest_journal_subquest_test.ts`.

### Stage 3: Tool Registration (`quest_journal_subquest`) & Verbal Protocol
- **Target**: Register custom tool and update system prompt instructions for verbal mid-quest remarks.
- **Tasks**:
  - Registered `quest_journal_subquest` and `task_journal_subquest` tools.
  - Updated system prompt instructions under `Mandatory Quest Workflow Rules` and `Quest Management & Verbal Requests` to mandate sub-quest creation and deep research for user remarks.
  - Updated docs in `docs/EXTENSIONS.md` and `.pi/prompts/help-setup.md`.
- **Targeted Tests**: `tests/unit/quest_journal_prompt_test.ts` and `tests/unit/quest_journal_subquest_test.ts`.

### Stage 4: End-to-End Verification & Quality Gates
- **Target**: Ensure zero regressions across existing unit tests, verify pure TypeScript loading, and run full test suite.
- **Tasks**:
  - Ran all unit tests: all passing with zero errors.
  - Verified no debug artifacts.

## Acceptance Criteria & Polish Checklist
- [x] `QUEST_TEMPLATE` includes `## Parent Quest` and `## Sub-Quests` sections.
- [x] `/subquest` command creates sub-quest file and links it bidirectionally in the parent quest file.
- [x] `quest_journal_subquest` tool allows model to programmatically create and link sub-quests.
- [x] System prompt includes explicit instructions on creating sub-quests for user remarks and enforcing deep research.
- [x] `/quests` widget lists sub-quests hierarchically under parent quests (`↳ <child>`).
- [x] Resume context loader includes parent/child quest information.
- [x] Comprehensive unit tests in `tests/unit/quest_journal_subquest_test.ts` pass.
- [x] `docs/EXTENSIONS.md` and `.pi/prompts/help-setup.md` updated with sub-quest documentation.
- [x] Zero compiler errors, zero debug artifacts, and all unit tests passing.

## Quest Refinements & User Feedback Loops
- User request: Added sub-quest feature for mid-quest remarks, creating independent quest files linked bidirectionally with the parent, requiring deep research and TDD.
- User feedback: Site C/e2e tests are not needed when working purely on extension tooling; focus on targeted unit tests (`tests/unit/quest_journal_*.ts`).

## Why this matters
During complex development quests, users frequently make observations, suggest tangent improvements, or raise questions that would derail immediate focus if tackled synchronously, but would be forgotten if not captured. Sub-quests provide clean encapsulation: separate disk-backed quest files that ensure deep research and full quality gates, while keeping the parent quest's roadmap organized.

## Decisions made
- Sub-quests are first-class quest files in `docs/current/` (or `docs/future/` if deferred) so they benefit from full session awareness, save gates, and compaction protection.
- Bidirectional markdown linking: parent points to child in `## Sub-Quests`, child points to parent in `## Parent Quest`.
- Provided both interactive `/subquest` slash-command and programmatic `quest_journal_subquest` tool.

## Constraints & Rules
- Pure TypeScript extension in `.pi/extensions/quest-journal.ts`.
- Compatible with existing aliases (`task`, `quest`).
- No site-specific JavaScript; maintain test purity.

## Files touched
- `docs/current/sub-quests.md`
- `.pi/extensions/quest-journal.ts`
- `tests/unit/quest_journal_subquest_test.ts`
- `tests/unit/quest_journal_prompt_test.ts`
- `docs/EXTENSIONS.md`
- `.pi/prompts/help-setup.md`

## Remaining work
- [x] Stage 1: Develop targeted unit test `tests/unit/quest_journal_subquest_test.ts` for sub-quest templates, parser, and link helper.
- [x] Stage 2: Implement sub-quest commands (`/subquest`) and hierarchical listing.
- [x] Stage 3: Implement `quest_journal_subquest` tool and update workflow system prompt instructions.
- [x] Stage 4: Update documentation and run full test verification.

## Open questions / risks
- None.

## Next recommended step
1. Quest is complete. Present completion choices to the user.

## Resume prompt
Sub-quests feature is completely implemented and verified with all unit tests passing.
