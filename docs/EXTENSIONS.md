# Project Extensions (`.pi/extensions/`)

This repository includes custom TypeScript extensions located in `.pi/extensions/` that extend Pi's functionality for quest persistence, session awareness, and project workflows.

---

## 1. Overview

Project-local extensions are loaded automatically by Pi when opening this repository (after trust confirmation).

- **Location**: `.pi/extensions/*.ts`
- **Hot-reload**: Execute `/reload` in Pi at any time to reload modified extensions, prompt files, and skills without restarting Pi.

---

## 2. Quest Journal & Context Awareness (`.pi/extensions/quest-journal.ts`)

### Purpose
Maintains long-lived quest state on disk (`docs/current/<quest>.md`), auto-injects persistent session awareness (timestamp, git branch, active quest freshness, standing project notes, auto-detected guidelines), and enforces strict TDD & quality gates across compaction and context resets.

### Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `/quest` | `/quest [name]` | Sets the active quest. When starting a new quest, prompts for a goal description, pre-populating `## Goal` in the template. When called without arguments, opens an interactive selector listing active quests, drafts, and an option for a new quest. (Alias: `/task`) |
| `/quest-refine` | `/quest-refine <instructions...>` | Refines the active quest mid-workflow or after initial implementation. Appends instructions to prompt history and instructs agent to update quest file goals, checklist, and `## Quest Refinements & User Feedback Loops`. (Alias: `/task-refine`) |
| `/quest-save` | `/quest-save` | Forces an immediate refresh prompt asking the agent to write a complete state snapshot to the active quest file. (Alias: `/task-save`) |
| `/quest-del` | `/quest-del [name]` | Archives the active or named quest file. When called without arguments, opens an interactive quest selector. Moves file to `docs/archive/<name>-<timestamp>.md` and cleans up any leftover draft. (Alias: `/task-del`) |
| `/quest-draft` | `/quest-draft <name>` | Creates a proposal draft in `docs/future/<name>.md` without making it active. Blocks creation if `<name>` is already active in `docs/current/`. (Alias: `/task-draft`) |
| `/quest-status` | `/quest-status` | Displays the active quest name, file freshness status (fresh vs. `SAVE PENDING`), context usage percentage, and prompt count. (Alias: `/task-status`) |
| `/quests` | `/quests` | Displays active quests in `docs/current/` and proposals in `docs/future/` in a widget panel. (Alias: `/tasks`) |

### Custom Tools

- **`quest_journal_mark_saved`**: A custom tool that records that the active quest file was written to disk. Automatically triggered whenever the model uses `write` or `edit` on `docs/current/<active>.md`. (Legacy alias: `task_journal_mark_saved`)
- **`quest_journal_archive`**: A custom tool to archive the active (or specified) quest from `docs/current/` to `docs/archive/` and optionally trigger session context compaction. (Legacy alias: `task_journal_archive`)

### Session Awareness & Context Injection

On every agent turn, the extension automatically injects into the system prompt:
- **Timestamp & Directory**: Current ISO timestamp and `ctx.cwd`.
- **Git Branch**: Active git branch read directly from `.git/HEAD` (zero subprocess overhead).
- **Active Quest State**: The active quest file path (`docs/current/<active>.md`) and whether its status is fresh or `SAVE PENDING`.
- **Active Quest Resume Context**: Summary of active goal, status, remaining work, and next step parsed directly from disk.
- **Project Guidelines**: Auto-detected mandatory project rules parsed from `AGENTS.md`, `CLAUDE.md`, or `SYSTEM.md` (extracts `## Guidelines`, `## Rules`, `## Invariants`, etc.).
- **Standing Notes**: Optional persistent project notes read from `.pi/context.md` (if the file exists).

### Mandatory TDD & Quality Workflow Rules

The extension automatically injects system prompt instructions and template sections forcing a strict TDD & Quality workflow for all quests:

1. **Build & Run Discovery**: Before editing feature code, the agent must discover how to build and run the project (e.g. read `AGENTS.md`, `Makefile`, scripts).
2. **Develop Tests First (TDD)**: One or multiple tests must be written for the quest **BEFORE** developing feature code.
3. **Iterative Build, Run & Test**: Implement feature -> Build project -> Run project -> Execute tests.
4. **Final Verification & Quality Gates**:
   - Build completes with **zero errors or warnings**.
   - Code contains **zero debug artifacts** (e.g., leftover `console.log`, debug prints, temporary comments).
   - The **full test suite** is executed at the end and passes with **zero errors**.

The quest template automatically includes a `## Build & Run Commands` section and a `## TDD & Quality Checklist` tracking these phases.

### Auto-behaviors & Compaction Gate

- **Prompt Capture**: Verbatim user prompts are automatically captured in session state and injected into save requests so the `## Original request` section in the quest file stays faithful.
- **Context Escalation**: Reminds the model to save the quest file as context fills up (>= 70% usage) and prompts for a save + `/compact` at >= 85% usage.
- **Compaction Gate**: Blocks `/compact` or auto-compaction unless the active quest file has been saved since the last compaction (`saveCount > compactCount`).
- **Startup Picker**: On TUI interactive startup, presents an interactive menu to choose an existing quest, promote a draft, start a new quest, or skip.
- **Session Shutdown / Switch**: Prompts for a state snapshot before switching sessions or exiting.

---

## 3. Standing Project Notes (`.pi/context.md`)

You can create or edit `.pi/context.md` at any time to give Pi standing facts or rules that should be injected into every request payload (e.g., "Always test with `make watch` after editing UX modules").

```markdown
# Standing Project Notes
- Run `make watch` after updating C UX modules.
- Ensure WASM bridge exports match `htdocs/*.wasm`.
```

---

## 4. Summary Table

| Extension File | Primary Hooks | Key Commands / Tools | Responsibility |
|----------------|---------------|----------------------|----------------|
| `.pi/extensions/quest-journal.ts` | `before_agent_start`, `turn_end`, `session_before_compact`, `session_compact`, `session_before_switch`, `session_shutdown`, `tool_result` | `/quest`, `/quest-refine`, `/quest-save`, `/quest-del`, `/quest-draft`, `/quest-status`, `/quests`, `quest_journal_mark_saved`, `quest_journal_archive` | Unified quest persistence, session awareness, prompt capture, compaction protection, TDD workflow enforcement |

---

## 5. Adding New Extensions

To create a new project-local extension:
1. Add a TypeScript file in `.pi/extensions/my-extension.ts`.
2. Export a default function taking `ExtensionAPI`:
   ```typescript
   import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";

   export default function (pi: ExtensionAPI) {
     pi.on("before_agent_start", async (event, ctx) => {
       // ...
     });
   }
   ```
3. Run `/reload` in Pi to load the new extension.
