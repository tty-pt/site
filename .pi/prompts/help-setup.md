---
description: Quick reference for this project's custom Pi setup
argument-hint: "[section]"
---

# Custom Setup Quick Reference

## Quest Workflow (quest-journal.ts)

| Command | What it does |
|---------|-------------|
| `/quest <name>` | Set active quest (promotes drafts or creates new) |
| `/quest-save` | Force-save current quest state to disk now |
| `/quest-refine <text>` | Add mid-workflow requirements to active quest |
| `/quest-del` | Archive completed quest to `docs/archive/` |
| `/quest-draft <name>` | Draft a future quest in `docs/future/` |
| `/quest-status` | Check if quest file is fresh or stale |
| `/quests` | List all current and future quests |

### How it works
- Each quest lives at `docs/current/<name>.md` — the single source of truth.
- On startup, you're prompted to pick or create a quest.
- The quest file is auto-saved before compaction and on context fill-up.
- Compaction is **blocked** until the quest file has been saved — protecting your work.
- `docs/future/` holds proposals/backlog; `/quest` promotes them to current.

## Session Awareness (context-awareness.ts)

Auto-injected into every system prompt turn:
- Current date/time and working directory
- Git branch (read from `.git/HEAD`)
- Active quest name and freshness status
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
