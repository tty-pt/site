# Project Extensions (`.pi/extensions/`)

This repository includes custom TypeScript extensions located in `.pi/extensions/` that extend Pi's functionality for quest persistence, session awareness, and project workflows.

---

## 1. Overview

Project-local extensions are loaded automatically by Pi when opening this repository (after trust confirmation).

- **Location**: `.pi/extensions/*.ts`
- **Hot-reload**: Execute `/reload` in Pi at any time to reload modified extensions, prompt files, and skills without restarting Pi.

---

## 2. Quest Journal & Context Awareness (`.pi/extensions/pi-quest/`)

### Purpose
Maintains long-lived quest state on disk (`.pi/quest/current/<qid>/quest.md`), auto-injects persistent session awareness (timestamp, git branch, active quest freshness, standing project notes, auto-detected guidelines), and enforces strict TDD & quality gates across compaction and context resets.

### Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `/quest` | `/quest [description|name]` | Sets the active quest. When initializing a new quest, the name is automatically inferred from the description without asking separately. When called without arguments, opens an interactive selector where selecting "New quest…" prompts directly for the description. |
| `/subquest` | `/subquest <description...>` | Creates a child sub-quest linked to the active quest with name inferred from description, updates parent's `## Sub-Quests` list, sets `## Parent Quest`, and activates the sub-quest. (Alias: `/sub-quest`) |
| `/quest-refine` | `/quest-refine <instructions...>` | Refines the active quest mid-workflow or after initial implementation. Appends instructions to prompt history and instructs agent to update quest file goals, checklist, and `## Quest Refinements & User Feedback Loops`. |
| `/quest-save` | `/quest-save` | Forces an immediate refresh prompt asking the agent to write a complete state snapshot to the active quest file. |
| `/quest-del` | `/quest-del [name]` | Archives the active or named quest. Moves runtime artifacts to `.pi/quest/archive/<qid>.zip`. |
| `/quest-draft` | `/quest-draft <description>` | Creates a proposal draft with name inferred from description without making it active. |
| `/quest-status` | `/quest-status` | Displays the active quest name, ID (`qid`), file freshness status (fresh vs. `SAVE PENDING`), context usage percentage, and prompt count. |
| `/quests` | `/quests` | Displays active quests in `.pi/quest/current/` in a widget panel. |

### Custom Tools

- **`quest_update_state`**: Update the active quest state on disk with structured fields (`status`, `findings`, `decisions`, `remaining`, `nextStep`, `plan`, `planConfidence`, `openQuestions`, `assumptions`, `exactNextAction`, etc.). Automatically formats, validates epistemic consistency, and deterministically writes `.pi/quest/current/<qid>/quest.md`.
- **`quest_mark_saved`**: Record that the active quest file has been written to disk. Automatically triggered whenever the model uses `write` or `edit` on `.pi/quest/current/<qid>/quest.md`.
- **`quest_subquest`**: Create a sub-quest for mid-quest remarks, tangents, or follow-ups. Inherits parent's `<qid>`, links it into the parent quest, and manages LIFO sub-quest stack activation.
- **`quest_archive`**: Archive the active (or specified) quest from `.pi/quest/current/<qid>/` to `.pi/quest/archive/<qid>.zip` and optionally trigger session context compaction.

### Autonomous Quest Management & Epistemic Memory

1. **Research-Grounded Formation**: When substantive requests arrive, the assistant investigates the codebase first, infers a semantic identity, and calls `quest_update_state` to establish durable epistemic memory before writing code.
2. **LIFO Sub-Quest Stack**: Deep work or multi-phase tasks use nested sub-quests (`quest_subquest`). When child sub-quests finish and archive (`quest_archive`), findings pop back to the parent quest for seamless continuation.
3. **Dynamic Epistemic Reassessment**: If unexpected complexity, failed tests, or contradictory evidence occurs, the assistant dynamically challenges its assumptions, updates `planRevisions` and `rejectedApproaches`, and updates the quest state before continuing.
4. **Autonomous Continuation**: Following compaction or context reset, the assistant immediately re-reads `.pi/quest/current/<qid>/quest.md` to resume without manual user commands.

### Session Awareness & Context Injection

On every agent turn, the extension automatically injects into the system prompt:
- **Timestamp & Directory**: Current ISO timestamp and `ctx.cwd`.
- **Git Branch**: Active git branch read directly from `.git/HEAD` (zero subprocess overhead).
- **Active Quest State**: The active quest file path (`.pi/quest/current/<qid>/quest.md`), quest ID (`qid`), and whether its status is fresh or `SAVE PENDING`.
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

### Uniform Model Error & Enforcement Messaging Architecture

`pi-quest` enforces a strict architectural invariant:
> **Any extension event that changes what the agent is permitted or required to do must produce a model-visible message describing the state change, the standardized `QuestErrorCode`, and the required next action.**
> UI notifications and diagnostic logs are never considered sufficient model feedback.

1. **Centralized Error Dispatch**: All enforcement blocks, gate rejections, persistence errors, save verification failures, and compaction directives pass through `reportAgentError()` and `sendInternalAgentMessage()`.
2. **Standardized Error Codes**:
   - `IMPLEMENTATION_BLOCKED`
   - `UNKNOWN_TOOL_BLOCKED`
   - `RESEARCH_REQUIRED`
   - `REASSESSMENT_REQUIRED`
   - `CONFIRMATION_REQUIRED`
   - `CHECKPOINT_REQUIRED`
   - `PERSISTENCE_FAILURE`
   - `SAVE_VERIFICATION_FAILURE`
   - `COMPACTION_FAILURE`
   - `CONTINUATION_FAILURE`
   - `STATE_RECONSTRUCTION_FAILURE`
   - `SUBQUEST_FAILURE`
   - `ARCHIVE_FAILURE`
3. **Explicit Degraded Durability**: If session persistence encounters an error, an explicit `PERSISTENCE_FAILURE` message is delivered to the agent warning that state will not survive compaction until persistence is restored and verified.

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
| `.pi/extensions/pi-quest/` | `before_agent_start`, `turn_end`, `session_before_compact`, `session_compact`, `session_before_switch`, `session_shutdown`, `tool_result` | `/quest`, `/subquest`, `/quest-refine`, `/quest-save`, `/quest-del`, `/quest-draft`, `/quest-status`, `/quests`, `quest_update_state`, `quest_mark_saved`, `quest_subquest`, `quest_archive` | Unified quest persistence, session awareness, prompt capture, compaction protection, TDD workflow enforcement |

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
