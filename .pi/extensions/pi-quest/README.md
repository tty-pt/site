# pi-quest v2

Gives your Pi coding agent a work ethic for long, complex tasks: it researches before it acts, writes a plan, gets the plan reviewed, and only then touches code — and it remembers everything across compactions and restarts, with almost no input needed from you.

## How a quest goes

1. **Drafting** — the agent may write exactly one file (the draft plan). Every change re-runs an adversarial reviewer until the plan PASSES. You can say "go" at any time to skip ahead; you are never required to.
2. **Implementing** — the agent works unrestricted from the plan. Surprises get recorded as amendments; nothing blocks.
3. **Validation** — a validator checks the work against the plan. PASS archives the quest; FAIL sends it back with findings.

Big tasks split into sub-quests, each with the same three phases.

Plans can change mid-way. If reality contradicts the plan itself, the agent revises the plan instead of abandoning the quest: every revision is re-reviewed, earlier plans are kept, a rejected revision snaps back to the last good plan — and validation judges the plan moves too, so the bar can't be quietly lowered to fit what was built.

## While you work

- Press `F2` any time to peek at the active quest's plan.
- The status bar always shows the active quest (dim; it flashes bright right after a draft change).
- A fresh session starts idle: nothing takes over until you describe a request or `/quest` resumes a known quest. With several known quests, `/quest` lets you pick one.

## The agent's tools

Six quest tools the main agent uses on your behalf: `quest_update_state` (record findings, plans, amendments — the write path), `quest_subquest` (spawn a sub-quest), `quest_archive` (finish as complete, failed, or abandoned), `quest_recover` (rebuild state from the transcript), `quest_rebut` (challenge a review with evidence), `quest_ask_human` (ask you with a recommended default and a one-minute timeout; never blocks).

## Requirements

- Pi coding agent (the extension loads itself; `/reload` picks up changes).
- pi-vcc with its override enabled, so compaction is deterministic and never touches quest state. Sub-agent extensions are optional — without one, reviews fall back to asking you.

## Your three commands

- `/quest` — resume a quest or drafting phase, or show the active one.
- `/quests` — list all quests with their states.
- `/quest-del` — archive (stop) the current or named quest.

Everything else happens by itself. Settings (all optional) live in `.pi/settings.json` under `"pi-quest"`: ask timeout, sub-quest depth cap, draft-review thresholds, interface bindings, status-bar style.

## Status & docs

Implemented and tested today: the full drafting → implementing → validation lifecycle with adversarial review, plan revision with re-review, sub-quests, idle boot with `/quest` picking, the `F2` plan viewer, and durability across compactions and restarts (218 tests pass; the distributable bundle is `pi-quest-bundle.zip` at the project root).

- Product (normative): `HIGH_LEVEL.md` (repo root)
- Behavior: `docs/PRODUCT_SPEC_v2.md` Part II
- Build plan: `REBUILD_PLAN.md` (repo root)
- Package rules: `AGENTS.md` (this directory)
