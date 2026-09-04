---
name: quest-journal
description: "Autonomous Quest Journal per AGENTS.md §7. Establishes .pi/quest/current/<qid>/quest.md as durable state for any substantive task (investigate, implement, fix, refactor, feature). Triggers on: quest, subquest, AGENTS.md, pi-quest, quest journal, .pi/quest, durable task, research-gated implementation"
---

# Quest Journal — Autonomous Workflow

Per `AGENTS.md:63` §7, `.pi/quest/current/<qid>/quest.md` is the single source of truth — never `.todo` or scratchpads. One renderer for the journal (`quest.md`); source of truth survives compaction.

## Quick start (frictionless)

1. **Research first** — explore files, run read-only probes. No edits yet.
2. **Establish durable quest on turn 1** — after research, immediately:
   ```ts
   quest_update_state({
     name, goal, status: "RESEARCH_COMPLETE",
     findings, decisions, plan, planConfidence,
     openQuestions, assumptions, remaining, nextStep, exactNextAction,
     researchComplete: true, planVersion: 1
   })
   ```
   Include `## Original request` verbatim + `## Build & Run Commands` + `## TDD & Quality Checklist`. Validates `QUEST_TEMPLATE`. This call deterministically writes `.pi/quest/current/<qid>/quest.md`.
3. **Implement against the quest** — gate `GATE_BLOCKED PROVISIONAL_RESEARCH_PENDING` (`persistence.ts:87`) rejects code edits until the quest is RESEARCH_COMPLETE. When a draft exists in `.pi/quest/future/<slug>.md`, author the proposal & implementation plan in that draft (via `edit`, `write`, or `quest_update_state`). The draft plan is reviewed by an adversarial subagent and promoted after reviewer APPROVE.
4. **Archive when done** — `quest_archive({questName, compact:true})` on `COMPLETED`/`FAILED` → `.pi/quest/archive/<qid>.zip` + `quest/diagnostic/current-run/`. Bundle with `npm --prefix .pi/extensions/pi-quest run zip [--quest=<qid>]`.

## Commands (14 — `src/commands/install.ts`)

- `/quest [<name>]` — set active quest (e.g. `/quest cx`). Creates `.pi/quest/current/<qid>/quest.md`.
- `/quest-save` — persist the active quest now.
- `/quest-refine <text>` — refine mid-workflow or add post-implementation requirements.
- `/quest-del [<name>]` — archive current or named quest.
- `/quest-draft <desc>` — draft a future quest/proposal (`future/<slug>.md`).
- `/quest-promote [slug]` — promote active draft to current after reviewer APPROVE (`go`).
- `/quest-economy [pct|bytes]` — token-economy auto-compaction threshold (`/quest-economy 50%`, `333k`, `off`).
- `/quest-warning [bytes]` — pre-compaction warning margin.
- `/quest-subquest-threshold [bytes]` — min tokens to auto-compact on subquest launch.
- `/quest-status` — active quest `qid`/freshness + current/future quest list.
- `/quests` — list current + future quests.
- `/subquest <goal> [name] [--no-switch]` / `/sub-quest` — create linked sub-quest (LIFO `state.stack`, `## Sub-Quests`/`## Parent Quest`). `switchNow:true` immediate, `false` pre-plan.

See `docs/EXTENSIONS.md §2` for command reference.

## Tools (6 — `src/tools/registration.ts`)

- `quest_update_state` — deterministic `quest.md` write. Params: `name, goal, status, findings, decisions, plan, planConfidence, openQuestions, assumptions, remaining, nextStep, exactNextAction, researchComplete, planVersion` (+ `questName` for non-active). Schema `QUEST_UPDATE_STATE_SCHEMA`.
- `quest_mark_saved` — mark `quest.md` saved (auto on `write`/`edit`; `QUEST_MARK_SAVED_SCHEMA`).
- `quest_subquest` — `{goal, name?, parentName?, switchNow?}` — genuine workstream / subsystem / investigation boundary.
- `quest_archive` — `{questName?, compact?, abandon?}` → `.pi/quest/archive/<qid>.zip` + optional compaction.
- `quest_rebut` — `{rebuttal, questName?}` — evidence-based rebuttal to last critical review (file:line citations); fresh reviewer pass, verdict reversal reopens gate, persisted to `## Review Dialogue`.
- `quest_ask_human` — `{question, context?}` — escalate to human with full dialogue transcript; sets `awaitingUserConfirmation`.

## Lifecycle

1. **Create/update** — `quest_update_state` per Quick start. Preserve `## Original request`; validates `QUEST_TEMPLATE`.
2. **Research-gated** — `researchComplete:true` resolves `RESEARCH_PENDING` (`state.ts` `PLAN_VERSION` / `persistence.ts` `GATE_BLOCKED`).
3. **Subquest** — `quest_subquest({goal, name?, parentName, switchNow})` — LIFO `state.stack`.
4. **Rebuttal/human** — `quest_rebut({rebuttal})` or `quest_ask_human({question})` when review blocks or decision needs human.
5. **Archive** — `quest_archive({questName, compact:true})`; handle `ARCHIVE_FAILURE` retry.
6. **Bundle** — `npm --prefix .pi/extensions/pi-quest run zip` → verify `pi-quest-bundle.zip` `SHA-256`/`Bundle Content SHA`, run `deno test` `83 tests (358 steps)`.

## Guardrails

- Forbid `.todo`/scratchpads/`bash mkdir` for quest files; drafts are authored in `.pi/quest/future/*.md` and active quests in `.pi/quest/current/<qid>/quest.md`.
- Remind `SAVE PENDING` before `70%/85%` context escalation and compaction (`saveCount > compactCount`).
- Session injection every turn: timestamp, `ctx.cwd`, `.git/HEAD` branch, active quest `qid`/freshness, resume context, `AGENTS.md` guidelines.
- Compaction safety: `session_start` self-installs this skill (`src/index.ts:84` copies `skills/quest-journal/SKILL.md` → `.pi/skills/quest-journal/SKILL.md`).

See `.pi/extensions/pi-quest/AGENTS.md`, `docs/ARCHITECTURE_MAP.md`
