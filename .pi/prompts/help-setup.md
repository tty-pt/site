---
description: Quick reference for this project's custom Pi setup
argument-hint: "[section]"
---

# Custom Setup Quick Reference

## Task Workflow (task-journal.ts)

| Command | What it does |
|---------|-------------|
| `/task <name>` | Set active task (promotes drafts or creates new) |
| `/task-save` | Force-save current task state to disk now |
| `/task-refine <text>` | Add mid-workflow requirements to active task |
| `/task-del` | Archive completed task to `docs/archive/` |
| `/task-draft <name>` | Draft a future task in `docs/future/` |
| `/task-status` | Check if task file is fresh or stale |
| `/tasks` | List all current and future tasks |

### How it works
- Each task lives at `docs/current/<name>.md` — the single source of truth.
- On startup, you're prompted to pick or create a task.
- The task file is auto-saved before compaction and on context fill-up.
- Compaction is **blocked** until the task file has been saved — protecting your work.
- `docs/future/` holds proposals/backlog; `/task` promotes them to current.

## Session Awareness (context-awareness.ts)

Auto-injected into every system prompt turn:
- Current date/time and working directory
- Git branch (read from `.git/HEAD`)
- Active task name and freshness status
- Project guidelines from `AGENTS.md` (auto-detected)
- Standing notes from `.pi/context.md` (create freely)

## Project-Specific

This is a pure-C music site. Key rules from AGENTS.md:
- Read `docs/OVERVIEW.md` before touching any code
- UX compiles twice: native `.so` (SSR) + `*.wasm` (browser)
- XY is the only cross-module boundary
- TDD is mandatory: tests first, then feature code
- Build: `make` / `make watch` on `:8080`

## Useful Shortcuts

- `/tree` — navigate session history
- `/fork` — branch from a previous point
- `/compact` — manual context compaction
- `/reload` — hot-reload extensions
- `Ctrl+P` — cycle models
- `Shift+Tab` — cycle thinking level
