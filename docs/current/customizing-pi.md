# Task: customizing-pi

## Goal
Make Pi handle **verbal** (natural-language) task-management requests — especially "refine this task" — by knowing how to update the active task file itself, as if it could always see the `/help-setup` quick reference.

## Original request
- "When I verbally ask pi to refine a task, I want it to know how to update the relevant task files. I want pi to know how to handle its own tasks. As if it can see the /setup-help thing."

## Current Status
- [x] Research complete
- [ ] Awaiting user decision: where to inject the command-map / verbal-handling instructions (A: task-journal.ts prompt block — recommended, B: APPEND_SYSTEM.md, C: context-awareness.ts reading help-setup.md)
- [ ] Implement chosen option
- [ ] Verify injection + end-to-end verbal refine test

## Build & Run Commands
- Extensions are plain `.ts` files loaded by pi from `.pi/extensions/` (no build step); `/reload` hot-reloads them.
- Config-only change; project `make` targets not involved.

## TDD & Quality Checklist
- [x] **1. Discovery**: Located all task-journal pieces (see Research).
- [ ] **2. Write Tests First**: Verification plan: inspect injected system prompt (e.g. `pi -p` session dump or debug print of event.systemPrompt once), then e2e: verbally ask pi to refine active task and confirm `docs/current/<task>.md` gains `## Task Refinements & Iterations` entry + updated sections.
- [ ] **3. Feature Implementation**: Add chosen instruction block.
- [ ] **4. Build & Run**: `/reload` (or restart pi) with zero errors.
- [ ] **5. Clean Code**: No debug artifacts left in extensions.
- [ ] **6. Full Test Suite**: Manual e2e pass; boundary scripts unaffected (no mods/ changes).

## Task Refinements & Iterations
- (none yet)

## Why this matters
Without this, verbal refinements rely on the model guessing the task-file format; the update recipe currently exists only inside command-triggered `sendSaveRequest()` strings, so plain-language requests produce inconsistent task files.

## Decisions made
- Scope: teach the model the command→file-update mapping for ALL task verbs (refine/switch/archive/draft/list), not just refine.
- Keep injected text compact (~12–15 lines) to bound per-turn token cost.

## Constraints & Rules
- Must stay consistent with `task-journal.ts` semantics (slugs, dirs: `docs/current|future|archive`, archive name `<name>-<base36 ts>.md`).
- Don't duplicate large content every turn; `Session awareness` already injects the active task path.

## Files touched
- (none yet — candidates: `.pi/extensions/task-journal.ts`, `.pi/APPEND_SYSTEM.md`, `.pi/extensions/context-awareness.ts`)

## Research / findings
- Commands live in `.pi/extensions/task-journal.ts`: `/task`, `/task-save`, `/task-refine`, `/task-del`, `/task-draft`, `/task-status`, `/tasks`, plus tool `task_journal_mark_saved`.
- Current system-prompt sources: `.pi/APPEND_SYSTEM.md` (workflow rules, NO command semantics); `installWorkflowSystemPrompt()` in task-journal.ts (appends Mandatory TDD rules); `context-awareness.ts` (injects Session awareness incl. active task path + freshness).
- Gap: nothing maps verbal requests to file updates. The refine recipe (expand Goal, add `## Task Refinements & Iterations`, update Remaining work/TDD checklist) exists only inside `sendSaveRequest()` used by `/task-refine` and hooks.
- Human-facing quick reference exists at `.pi/prompts/help-setup.md` but is only rendered when the user invokes it.
- Options:
  - **A (recommended)**: extend `installWorkflowSystemPrompt()` in task-journal.ts with a compact "Command map & verbal handling" block — stays in sync with the code implementing the commands.
  - **B**: append same content to `.pi/APPEND_SYSTEM.md` — simplest, zero code, but risks drifting from the extension.
  - **C**: context-awareness.ts injects `help-setup.md` content — full table every turn, heavier tokens, duplicates the prompt file.

## Remaining work
- [ ] User picks A/B/C
- [ ] Implement + `/reload`
- [ ] Verify: injected prompt contains block; verbal "refine the task: …" updates the file correctly
- [ ] Optionally persist durable note to memory

## Open questions / risks
- Token overhead per turn (mitigated by keeping block ~150 words).
- Drift between instructions and extension behavior if B chosen.

## Next recommended step
1. Get user's choice (A recommended), implement the block, reload, run the verification plan above.

## Resume prompt
User wants pi to self-manage task files on verbal requests. Research done: gap is missing command-map/verbal-refine instructions in the system prompt; recipe currently only in sendSaveRequest(). Decide injection point (A task-journal.ts recommended / B APPEND_SYSTEM.md / C context-awareness.ts), implement compact block, reload, verify e2e.
