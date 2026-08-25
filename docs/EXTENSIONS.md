# Project Extensions (`.pi/extensions/`)

This repository includes custom TypeScript extensions located in `.pi/extensions/` that extend Pi's functionality for task persistence, context awareness, and project workflows.

---

## 1. Overview

Project-local extensions are loaded automatically by Pi when opening this repository (after trust confirmation).

- **Location**: `.pi/extensions/*.ts`
- **Hot-reload**: Execute `/reload` in Pi at any time to reload modified extensions, prompt files, and skills without restarting Pi.

---

## 2. Task Journal (`.pi/extensions/task-journal.ts`)

### Purpose
Maintains long-lived task state on disk (`docs/current/<task>.md`) so work survives across Pi sessions, context resets, and auto-compactions without re-researching or losing context.

### Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `/task` | `/task [name]` | Sets the active task. When starting a new task, prompts for a goal description, pre-populating `## Goal` in the template. When called without arguments, opens an interactive selector listing active tasks, drafts, and an option for a new task. |
| `/task-refine` | `/task-refine <instructions...>` | Refines the active task mid-workflow or after initial implementation. Appends instructions to prompt history and instructs agent to update task file goals, checklist, and `## Task Refinements & Iterations`. |
| `/task-save` | `/task-save` | Forces an immediate refresh prompt asking the agent to write a complete state snapshot to the active task file. |
| `/task-del` | `/task-del [name]` | Archives the active or named task file. When called without arguments, opens an interactive task selector. Moves file to `docs/archive/<name>-<timestamp>.md` and cleans up any leftover draft. |
| `/task-draft` | `/task-draft <name>` | Creates a proposal draft in `docs/future/<name>.md` without making it active. Blocks creation if `<name>` is already active in `docs/current/`. |
| `/task-status` | `/task-status` | Displays the active task name, file freshness status (fresh vs. `SAVE PENDING`), context usage percentage, and prompt count. |
| `/tasks` | `/tasks` | Displays active tasks in `docs/current/` and proposals in `docs/future/` in a widget panel. |

### Custom Tools

- **`task_journal_mark_saved`**: A custom tool that records that the active task file was written to disk. Automatically triggered whenever the model uses `write` or `edit` on `docs/current/<active>.md`.

### Mandatory TDD & Quality Workflow Rules

The extension automatically injects system prompt instructions and template sections forcing a strict TDD & Quality workflow for all tasks:

1. **Build & Run Discovery**: Before editing feature code, the agent must discover how to build and run the project (e.g. read `AGENTS.md`, `Makefile`, scripts).
2. **Develop Tests First (TDD)**: One or multiple tests must be written for the task **BEFORE** developing feature code.
3. **Iterative Build, Run & Test**: Implement feature -> Build project -> Run project -> Execute tests.
4. **Final Verification & Quality Gates**:
   - Build completes with **zero errors or warnings**.
   - Code contains **zero debug artifacts** (e.g., leftover `console.log`, debug prints, temporary comments).
   - The **full test suite** is executed at the end and passes with **zero errors**.

The task template automatically includes a `## Build & Run Commands` section and a `## TDD & Quality Checklist` tracking these phases.

### Auto-behaviors & Compaction Gate

- **Prompt Capture**: Verbatim user prompts are automatically captured in session state and injected into save requests so the `## Original request` section in the task file stays faithful.
- **Context Escalation**: Reminds the model to save the task file as context fills up (>= 70% usage) and prompts for a save + `/compact` at >= 85% usage.
- **Compaction Gate**: Blocks `/compact` or auto-compaction unless the active task file has been saved since the last compaction (`saveCount > compactCount`).
- **Startup Picker**: On TUI interactive startup, presents an interactive menu to choose an existing task, promote a draft, start a new task, or skip.
- **Session Shutdown / Switch**: Prompts for a state snapshot before switching sessions or exiting.

---

## 3. Context Awareness (`.pi/extensions/context-awareness.ts`)

### Purpose
Models have no persistent awareness of your environment outside of what is in their request context payload. This extension appends a compact `# Session awareness` block to the system prompt before every agent turn via `before_agent_start`.

### Auto-Injected Facts

On every agent turn, the model automatically sees:
- **Timestamp & Directory**: Current ISO timestamp and `ctx.cwd`.
- **Git Branch**: Active git branch read directly from `.git/HEAD` (zero subprocess overhead).
- **Task Journal State**: The active task file path (`docs/current/<active>.md`) and whether its status is fresh or `SAVE PENDING`.
- **Project Guidelines**: Auto-detected mandatory project rules parsed from `AGENTS.md`, `CLAUDE.md`, or `SYSTEM.md` (extracts `## Guidelines`, `## Rules`, `## Invariants`, etc.).
- **Standing Notes**: Optional persistent project notes read from `.pi/context.md` (if the file exists).

### Standing Project Notes (`.pi/context.md`)

You can create or edit `.pi/context.md` at any time to give Pi standing facts or rules that should be injected into every request payload (e.g., "Always test with `make watch` after editing UX modules").

```markdown
# Standing Project Notes
- Run `make watch` after updating C UX modules.
- Ensure WASM bridge exports match `htdocs/*.wasm`.
```

---

## 4. Summary Table

| Extension File | Primary Hook | Key Commands / Tools | Responsibility |
|----------------|--------------|----------------------|----------------|
| `.pi/extensions/task-journal.ts` | `before_agent_start`, `turn_end`, `session_before_compact`, `tool_result` | `/task`, `/task-refine`, `/task-save`, `/task-del`, `/task-draft`, `/task-status`, `/tasks`, `task_journal_mark_saved` | Disk-backed state persistence, task refinement, compaction protection, prompt capture |
| `.pi/extensions/context-awareness.ts` | `before_agent_start` | Read `AGENTS.md`/`CLAUDE.md`/`SYSTEM.md`, `.pi/context.md`, `.git/HEAD` | Per-turn system prompt context injection (date, branch, active task, project guidelines, standing notes) |

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
