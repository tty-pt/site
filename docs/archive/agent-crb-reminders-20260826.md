# Task: agent-crb-reminders

## Goal
Implement a lightweight, global, dynamic Context Reinforcement Block (CRB) and Tool Awareness extension (`~/.pi/agent/extensions/crb.ts`). It reinforces available tools, general agent discipline, and project conventions at session boot and dynamically every ~20K tokens (as well as on demand via `/crb`, `/tools`, `/rules`), adapting dynamically to whichever tools and project guidelines exist in the active workspace without hardcoding project-specific assumptions.

## Original request
- "I want each pi session I start to be reminded of available tools, alongside important things like how to use tasks, and what conventions to follow. At boot and every once in a while a small crb. Maybe an extension could have a command for a more complete information and then have the small context reinforcement block to remind the agent of important rules and tools and stuff. Agents repeat the same mistakes all over again, even though they are clearly identified. Especially in long sessions. Let's brainstorm. Start a task for this. I'll tell you if I'm happy with the plan."
- Refinement 1: "Global and dynamic is best. Every 20K tokens, maybe. Yeah. But keep it simple."
- Refinement 2: "Wait... That crb seems specific to this project. That's not the idea." -> Fully project-agnostic. Dynamically derive tool rules based on active tools and project guidelines strictly from the local environment, with zero hardcoded project assumptions in the global extension.

## Current Status
- [x] Initial codebase & Pi extension research complete
- [x] Architecture brainstormed and approved by user
- [x] Refinement applied: remove all hardcoded site-specific rules; make tool rules and project guidelines 100% dynamic
- [x] Implemented `~/.pi/agent/extensions/crb.ts`
- [x] Verified generic vs project-specific dynamic generation via automated test suite
- [x] Quality gates passed with zero errors

## Build & Run Commands
- Global Pi extensions reside in `~/.pi/agent/extensions/*.ts` and auto-load on start.
- Hot reload inside any running Pi session: `/reload`
- Manual command checks: `/crb`, `/tools`, `/rules`

## TDD & Quality Checklist
- [x] **1. Discovery**: Investigated dynamic tool detection and dynamic guidelines extraction.
- [x] **2. Write Tests / Verification First**: Built automated unit verification script for jiti loader, lifecycle event hooks, token bucket detection, command handlers, and generic vs customized workspace scenarios.
- [x] **3. Feature Implementation**: Refactored `~/.pi/agent/extensions/crb.ts` to be 100% dynamic and project-agnostic.
- [x] **4. Build & Run**: Clean execution without errors on load or turn.
- [x] **5. Clean Code**: No temporary debug logs or artifacts left.
- [x] **6. Full Verification**: Checked module boundary scripts and validated all commands and event handlers.

## Task Refinements & Iterations
- 2026-08-26: The global CRB was refactored so that it has zero hardcoded project rules. It conditionally generates tool discipline based on the session's active tools (e.g. `read`, `edit`, MCP graph tools, memory tools, background tools), and auto-detects workspace rules from `AGENTS.md` / `CLAUDE.md` / `SYSTEM.md` only when they exist.

## Decisions made
- **Location**: Global extension at `~/.pi/agent/extensions/crb.ts` so all Pi sessions across all repositories benefit immediately.
- **Dynamic Tool Rules**: Tool rules are generated conditionally based on which tools are actually active:
  - If `read` is active: "Use `read` to examine files (never cat/sed/head via bash)."
  - If `edit` is active: "Use `edit` for surgical modifications with exact text matches; never rewrite existing files with `write`."
  - If MCP graph tools (`search_graph`, etc.) are active: "Prefer MCP codebase graph tools over grep/find for code discovery."
  - If task journal tool (`task_journal_mark_saved`) is active: "Call `task_journal_mark_saved` after updating active task files."
  - If memory tools (`memory_write`, etc.) are active: "Persist durable decisions and facts with `memory_write` / `scratchpad`."
  - If background tools (`bg_run`, `subagent`, etc.) are active: "Use `bg_run` / `bg_delegate` / `subagent` for long-running or delegated workloads."
- **Dynamic Project Invariants**: Extracted dynamically from local markdown documentation (`AGENTS.md`, `CLAUDE.md`, `SYSTEM.md`, `.pi/context.md`) if present in `cwd`.
- **Cadence**: Every 20K tokens (tracked via `ctx.getContextUsage()?.tokens`), plus boot injection on `before_agent_start`, plus on-demand via `/crb`.
- **Ephemeral Injection**: Uses the `context` hook (`pi.on("context", ...)`) to inject the CRB ephemeral user/system message right before the LLM call.

## Constraints & Rules
- Must keep CRB compact (~10–16 lines max).
- Must never throw or break an agent turn if a file is missing or unreadable.
- Must not duplicate stored session messages.

## Files touched
- `docs/current/agent-crb-reminders.md`
- `/home/quirinpa/.pi/agent/extensions/crb.ts`

## Remaining work
- None! Feature implemented, tested, and verified.

## Next recommended step
Notify user of completion.
