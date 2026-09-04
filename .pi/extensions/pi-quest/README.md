# pi-quest v2

Gives your Pi coding agent a work ethic for long, complex tasks: it researches before it acts, writes a plan, gets the plan reviewed, and only then touches code — and it remembers everything across compactions and restarts, with almost no input needed from you.

## How a quest goes

1. **Drafting** — the agent may write exactly one file (the draft plan). Every change re-runs an adversarial reviewer until the plan PASSES. You can say "go" at any time to skip ahead; you are never required to.
2. **Implementing** — the agent works unrestricted from the plan. Surprises get recorded as amendments; nothing blocks.
3. **Validation** — a validator checks the work against the plan. PASS archives the quest; FAIL sends it back with findings.

Big tasks split into sub-quests, each with the same three phases.

## Requirements

- Pi coding agent (the extension loads itself; `/reload` picks up changes).
- pi-vcc with its override enabled, so compaction is deterministic and never touches quest state. Sub-agent extensions are optional — without one, reviews fall back to asking you.

## Your three commands

- `/quest` — resume a quest or drafting phase, or show the active one.
- `/quests` — list all quests with their states.
- `/quest-del` — archive (stop) the current or named quest.

Everything else happens by itself. Settings (all optional) live in `.pi/settings.json` under `"pi-quest"`: ask timeout, sub-quest depth cap, draft-review thresholds, interface bindings.

## Status & docs

Status: skeleton — loads clean, no quest behavior yet. Slices land per `REBUILD_PLAN.md` (repo root).

- Product (normative): `HIGH_LEVEL.md` (repo root)
- Behavior: `docs/PRODUCT_SPEC_v2.md` Part II
- Build plan: `REBUILD_PLAN.md` (repo root)
- Package rules: `AGENTS.md` (this directory)
