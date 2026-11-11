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
Maintains long-lived quest state on disk (`.pi/quest/future/<qid>.md`), auto-injects persistent session awareness (timestamp, git branch, active quest freshness, standing project notes, auto-detected guidelines), and enforces strict TDD & quality gates across compaction and context resets.

### Commands

All operator commands are unified under `/quest` (with interactive tab-completion for subcommands and quest names) plus the `Alt+Q` session toggle shortcut:

| Command | Usage | Description |
|---------|-------|-------------|
| `/quest` | `/quest` | Shows the self-explaining quest status card (active quest, phase, plan path, next action, and subcommand reference), or auto-resumes a lone draft when idle. |
| `/quest <name\|qid>` | `/quest [resume] <name\|qid>` | Switches to or resumes a quest by its readable draft name or QID. |
| `/quest new` | `/quest new [goal]` (or `/quest chain\|next`) | Retires the current quest while keeping its scratch slot (`.pi/scratch/slot-N`) and branch intact, re-enables `pi-quest`, and starts a new quest on top of that slot's work (inferring the goal from prior conversation when `[goal]` is omitted or referential). |
| `/quest finish` | `/quest finish [--keep-slot]` (or `/quest merge\|integrate`) | Completes and integrates the active quest, or squash-merges a retained scratch slot (`.pi/scratch/slot-N`) into `main` after manual testing (`--keep-slot` to keep the slot attached after squash-merging). |
| `/quest edit` | `/quest edit` (or `/quest plan`) | Opens the active quest plan file (`.pi/quest/future/<qid>.md`) in `$EDITOR` (or `planEditor`). |
| `/quest list` | `/quest list` (or `/quest ls`) | Lists active & draft quests in `.pi/quest/future/` with triage verdicts (`active`, `fresh`, `stale`, `orphan`, `empty-shell`), age, requirement progress, and active worktree slot. |
| `/quest pick` | `/quest pick` (or `/quest switch`) | Interactively selects a quest to resume. |
| `/quest pause` / `/quest on` | `/quest pause\|off\|on [goal]\|toggle` (`Alt+Q`) | Pauses, re-enables, or toggles `pi-quest` for the current session (`⏸️` in status bar; starts disabled by default unless `"enabled": true` is set in `.pi/settings.json`). Passing `/quest on [goal]` (or activating after brainstorming) starts a quest immediately using the prior conversation context. |
| `/quest cancel` | `/quest cancel` (or `/quest cancel-review`) | Cancels any currently running in-flight review or validation and unblocks tool execution. |
| `/quest del` | `/quest del [qid\|flags]` (or `/quest drop`) | Deletes/abandons the active quest (`--keep-slot` / `-s` to keep slot work intact, `--integrate` / `-i`, `--keep` / `-k`, `--discard` / `-d`), a named draft quest, or batch-prunes drafts (`--all`, `--stale`, `--empty`, `--orphans`, `--days N`, `--dry-run`) without creating `.zip` files. |
| `/quest handoff` | `/quest handoff [qid]` | Generates a deterministic, honest handoff document to `.pi/quest/handoffs/<qid>-<stamp>.md` and displays it. Auto-runs on compaction. |
| `/quest slots` | `/quest slots` (or `/quest scratches`) | Inspects active scratch workspace slots (`.pi/scratch/slot-N`), assigned quests, process health, and working tree state. |
| `/quest doctor` | `/quest doctor` | Checks judge endpoint health, latency, models, core C engine versions, and trigger word families. |
| `/quest policy` | `/quest policy` | Shows the effective review policy for the active model, draft quality bar, and config warnings. |
| `/quest router` | `/quest router` | Inspects phase × difficulty virtual model router status, matrix, and cooldowns. |
| `/quest ratings` | `/quest ratings` | Inspects model accuracy ratings, learned Beta distributions, and autonomy levels. |

### Common Operator Workflows

1. **Brainstorm with Argon First, Then Activate a Quest (`/quest on`, `"quest: Go for it."`, `/quest new`)**:
   - Sessions start in `idle` (or paused `⏸️`) routed to **`antigravity-acp/gemini-4-argon:high`** so you can discuss architecture, explore files, and agree on a plan conversationally without premature quest gates.
   - Once you are happy with the understanding/plan, activate a quest with `"quest: Go for it."`, `/quest on`, `/quest on [goal]`, or `/quest new [goal]`.
   - `pi-quest` automatically extracts the substantive objective from the prior conversation transcript (`extractPriorConversationObjective`) rather than recording `"Go for it."` literally, transitions directly into plan authoring (`.pi/quest/future/<qid>.md`), and runs the `[Drafter–Reviewer Dialogue]` in the background while you continue chatting.
2. **Toggle or pause quest enforcement (`Alt+Q` / `/quest off` / `/quest on`)**:
   - Press **`Alt+Q`** (or run `/quest off` / `/quest pause`) to pause `pi-quest` at any time and work freely without draft gates or automatic quest creation (while keeping the router on the `idle` Argon rule).
   - Press **`Alt+Q`** again (or run `/quest on`) to resume the paused quest.
3. **Test in the scratch worktree before or after merging (`keep-slot`, `squash-keep-slot`, `/quest finish`)**:
   - When completing a quest (`quest_archive` or interactive archive prompt), choose:
     - **`keep-slot` (Leave in worktree slot for manual testing before merge)**: Keeps the completed quest unmerged in `.pi/scratch/slot-N` so you can test or inspect it manually before integrating into `main`.
     - **`squash-keep-slot` (Squash-merge into main AND keep worktree slot alive)**: Squash-merges into `main` while keeping `slot-N` checked out at the new `main` HEAD.
   - Once manual testing in a retained slot is finished, run **`/quest finish`** (or `/quest merge` / `/quest integrate`, optionally with `--keep-slot`) to squash-merge the branch into `main` and release the slot.
4. **Continue work in the same scratch slot under a new quest (`/quest new`)**:
   - When an active or paused quest is getting in the way and you want to plan a new quest on top of the current `.pi/scratch/slot-N` worktree without merging or resetting the slot:
     ```text
     /quest new <new objective>
     ```
   - Or run bare `/quest new` (or `/quest del --keep-slot`) to retire the old quest while keeping `slot-N` attached, then describe your next task in chat.
5. **Delete a quest without freeing or resetting its slot (`/quest del --keep-slot`)**:
   - Run `/quest del --keep-slot` (or `/quest del -s`, or select `"Keep work in slot (do not reset or free slot)"` in `/quest del`) to delete the quest record while keeping `.pi/scratch/slot-N` and its uncommitted/committed changes intact.
   - Run `/quest del --discard` while idle to release and reset a retained slot.
6. **Stay in-quest during validation or archive (`decline` / `Cancel archive`)**:
   - Reply **`decline`** (or give corrective feedback) during `validating`, or select **`"Cancel archive (stay in quest)"`** (or press `Escape`) at the branch integration prompt to demote back to `implementing` with tools unlocked.

### Custom Tools

- **`quest_update_state`**: Primary write path (`objective`, `draftName`, `difficulty`, `plan`, `analysis`, `planRevision`, `claimComplete`, `findings`, `decisions`, `remaining`, `nextStep`, `exactNextAction`, etc.). Supports atomic single-call quest creation, plan authoring, mid-flight plan revision, and completion claims, writing `.pi/quest/future/<qid>.md`.
- **`quest_subquest`**: Create a sequential child sub-quest (`goal`, `brief`, `allowedPaths`, `requiredTestCommands`) on the LIFO stack, inheriting the parent's worktree slot.
- **`quest_recover`**: Rebuild quest state from the transcript after context resets or compaction.
- **`quest_handoff`**: Write a deterministic, honest handoff for the next agent into `.pi/quest/handoffs/` including verbatim operator requests, deduped directives, Asked-vs-Accomplished checklist with UNVERIFIED markers, approved plan, git diffstat, last verification, blockers, and agent notes.
- **`quest_archive`**: Conclude and archive the active quest (`outcome: "completed"` requires validation PASS; optional `action: "integrate" | "keep" | "keep-slot" | "discard"` controls worktree disposition), integrating or retaining the worktree branch and cleaning up `.pi/quest/future/<qid>.md` directly without `.zip` files.
- **`quest_scratches`**: Inspect scratch workspace slots (`.pi/scratch/slot-N`): view lease status, assigned quests, process health, git status, and what each quest is currently doing.
- **`quest_run_test`**: Safely execute a verification test command inside the active worktree (or primary repository root). Records the verification test receipt.
- **`quest_rebut`**: Answer an adversarial review with file:line evidence; a successful rebuttal reopens the question and boots a fresh review.
- **`quest_ask_human`**: Ask the user with a recommended default and timeout; never blocks.

### Autonomous Quest Management, Drafter–Reviewer Dialogue & Epistemic Memory

1. **Research-Grounded Formation & Earlier Plan Authoring**: When substantive requests arrive (or when a quest is activated after a conversation with Argon), the assistant authors `.pi/quest/future/<qid>.md` immediately. Editing `.pi/quest/future/<qid>.md` while in `provisional` automatically promotes the quest to `drafting` with `planAuthored: true`.
2. **Turn-Coalesced Reviews & `[Drafter–Reviewer Dialogue]`**: Passive file edits to the draft plan coalesce within a turn and boot a single adversarial reviewer at `turn_end` (preventing mid-turn self-aborts). Resolved review rounds are recorded in `reviewHistory` and injected as structured `[Drafter–Reviewer Dialogue]` blocks so the drafter and reviewer converge while the operator can continue chatting concurrently.
3. **LIFO Sub-Quest Stack**: Deep work or multi-phase tasks use nested sub-quests (`quest_subquest`). When child sub-quests finish and archive (`quest_archive`), findings pop back to the parent quest for seamless continuation.
4. **Dynamic Epistemic Reassessment**: If unexpected complexity, failed tests, or contradictory evidence occurs, the assistant dynamically challenges its assumptions, updates `planRevisions` and `rejectedApproaches`, and updates the quest state before continuing.
5. **Autonomous Continuation**: Following compaction or context reset, the assistant immediately re-reads `.pi/quest/future/<qid>.md` to resume without manual user commands.

### Session Awareness & Context Injection

On every agent turn, the extension automatically injects into the system prompt:
- **Timestamp & Directory**: Current ISO timestamp and `ctx.cwd`.
- **Git Branch**: Active git branch read directly from `.git/HEAD` (zero subprocess overhead).
- **Active Quest State**: The active quest file path (`.pi/quest/future/<qid>.md`), quest ID (`qid`), and whether its status is fresh or `SAVE PENDING`.
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

## 3. Persistent Memory Mipmaps (`.pi/extensions/pi-mm/`)

### Purpose
Provides multi-resolution, hierarchical episodic memory (L0 raw tool events $\rightarrow$ L1 task episodes $\rightarrow$ L2 project invariants $\rightarrow$ L3 system principles) powered by `libcorm`, `libsepal`, `liblemma`, and `axil-auren`. Automatically captures engineering breakthroughs and standing directives across sessions, reinforces active constraints in prompt context, and enables fast semantic and keyword recall.

### Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `/mm doctor` | `/mm doctor` | Displays live status for decision judge, vector endpoints, storage footprint, and consolidation pipeline health (L2/L3 synthesis status, L0/L1 queue depths, drain rate, and backlog alerts). |
| `/mm overview` | `/mm overview` | Synthesizes an all-time structured timeline across active principles (L3), invariants (L2), and milestones (L1). |
| `/mm timeline` | `/mm timeline` | Chronological grouping of memory capture epochs and historical activity. |

### Tools

- **`memory_recall`**: Multi-resolution retrieval (`query`, `overview`, `zoomRef`, `project`, `level`, `includeEvidence`). Searches semantically via `libsepal` and lexically via `liblemma`, supports all-time timeline overviews (`overview: true`), and zooms into full evidence trees (`zoomRef: 42`).
- **`memory_record`**: Record structured task episodes (`problem`, `approaches`, `diffs`, `findings`, `outcome`, `verification`). Automatically elevates standing operational rules (`standing: true`) to persistent L2 invariants.
- **`memory_review`**: Inspect memory evidence hierarchy (`action: "inspect"`) or transition factual lifecycle status (`action: "update_lifecycle"`: tested, supported, contradicted, superseded).

### Key Architectural Invariants & Mechanics

1. **Continuous Directive Reinforcement**: Mid-quest developer corrections and standing negative constraints are detected via `POST /v1/systemone`, elevated to L2 invariants, and reinforced newest-first in the agent's prompt across every turn.
2. **Unwedged Consolidation & Idle Backlog Drain**: Fresh turn events are prioritized first, with remaining backlogs ordered newest-first (`orderPendingL0Refs`). Idle turns drain backlogged L0 records within a configurable wall-clock budget (`consolidateDrainMs`, default 250ms).
3. **Zero-Config Local Chat Synthesis**: Automatically hooks local `axil-auren` OpenAI-compatible chat endpoints (`/v1/chat/completions`) using `smollm` for L2/L3 abstraction synthesis.
4. **Targeted Recall & Episode Exemption**: Hybrid recall surfaces relevant L0 memories when requested (`level: "L0"` or untiered searches) while strictly exempting structured technical episodes from code artifact pruning.

---

## 4. Standing Project Notes (`.pi/context.md`)

You can create or edit `.pi/context.md` at any time to give Pi standing facts or rules that should be injected into every request payload (e.g., "Always test with `make watch` after editing UX modules").

```markdown
# Standing Project Notes
- Run `make watch` after updating C UX modules.
- Ensure WASM bridge exports match `htdocs/*.wasm`.
```

---

## 5. Summary Table

| Extension File | Primary Hooks | Key Commands / Tools | Responsibility |
|----------------|---------------|----------------------|----------------|
| `.pi/extensions/pi-quest/` | `before_agent_start`, `turn_end`, `session_before_compact`, `session_compact`, `session_before_switch`, `session_shutdown`, `tool_result` | `/quest [edit\|list\|pick\|new\|finish\|pause\|on\|toggle\|cancel\|del\|handoff\|slots\|doctor\|policy\|router\|ratings]`, `quest_update_state`, `quest_subquest`, `quest_archive`, `quest_handoff`, `quest_scratches`, `quest_run_test`, `quest_rebut`, `quest_ask_human`, `quest_recover` | Unified quest persistence, Argon-first routing, turn-coalesced Drafter–Reviewer dialogue, worktree slot isolation & manual test retention, compaction handoffs, TDD workflow enforcement |
| `.pi/extensions/pi-mm/` | `before_agent_start`, `turn_end`, `agent_settled`, `session_start`, `session_shutdown`, `message_end`, `tool_result` | `/mm doctor`, `/mm overview`, `/mm timeline`, `memory_recall`, `memory_record`, `memory_review` | Hierarchical memory mipmaps, epistemic lifecycle verification, standing directive reinforcement, automated L0-L3 consolidation |

---

## 6. Adding New Extensions

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
