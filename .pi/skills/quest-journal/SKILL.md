---
name: quest-journal
description: "Autonomous quest journal workflow per AGENTS.md §7. Use for any substantive task. Triggers on: quest, subquest, AGENTS.md, pi-quest, quest journal, .pi/quest"
---

# Quest Journal — Autonomous Workflow

Per `AGENTS.md:63-75` §7, `.pi/quest/current/<qid>/quest.md` is the single source of truth — never `.todo` or scratchpads.

## Commands & tools

- **Commands:** `/quest` `/subquest` (`/sub-quest`) `/quest-refine` `/quest-save` `/quest-del` `/quest-draft` `/quest-status` `/quests` (`docs/EXTENSIONS.md §2`).
- **Tools (4):** `quest_update_state` (structured `status/findings/decisions/remaining/nextStep/plan/planConfidence/openQuestions/assumptions/exactNextAction` → deterministic write `.pi/quest/current/<qid>/quest.md`), `quest_mark_saved` (auto on `write/edit` of quest.md), `quest_subquest` (inherits parent `<qid>` LIFO stack `## Sub-Quests` + `## Parent Quest`), `quest_archive` (→ `.pi/quest/archive/<qid>.zip` + optional compaction).

## Lifecycle

1. **Create/update** — after research, call `quest_update_state({name, goal, status, findings, decisions, plan, planConfidence, openQuestions, assumptions, remaining, nextStep, exactNextAction, researchComplete:true, planVersion})`; preserve `## Original request` verbatim; include `## Build & Run Commands` + `## TDD & Quality Checklist`. Validates `QUEST_TEMPLATE`.

2. **Research-gated** — gates `GATE_BLOCKED PROVISIONAL_RESEARCH_PENDING` `persistence.ts:87` `GATE_BLOCKED` `future_draft_exists → requiredAction=quest_update_state` — if `Draft exists in .pi/quest/future/<slug>.md — call quest_update_state (not quest_mark_saved or bash mkdir)` then one-call `quest_update_state({researchComplete:true})` → `STATE_UPDATE_ACCEPTED` + `DRAFT_DISCARDED` + `future-archive/` (`executor.ts:63 syncQuestIdentity`).

3. **Subquest** — `quest_subquest({goal, name?, parentName, switchNow})` — `true` immediate tangent, `false` pre-planning; auto-links `## Sub-Quests`/`## Parent Quest`, LIFO `state.stack`.

4. **Archive** — `quest_archive({questName, compact:true})` on `COMPLETED`/`FAILED` → `.pi/quest/archive/<qid>.zip` (verify `zipExists`), then `quest/diagnostic/current-run/`; handle `ARCHIVE_FAILURE` retry.

5. **Bundle** — `npm --prefix .pi/extensions/pi-quest run zip [--quest=<qid>]` → verify `pi-quest-bundle.zip` `SHA-256`/`Bundle Content SHA`, run `deno test` 178 steps.

## Guardrails

- Forbid `.todo`/scratchpads/`bash mkdir` for quest files; enforce `quest_update_state` when `FUTURE_DIR/*.md` exists.
- Remind `SAVE PENDING` before `70%/85%` context escalation and compaction (`saveCount > compactCount`).
- Session injection every turn: timestamp, `ctx.cwd`, `.git/HEAD` branch, active quest `qid`/freshness, resume context, `AGENTS.md` guidelines.

See `.pi/extensions/pi-quest/AGENTS.md` `docs/EXTENSIONS.md` `# High-Level Plan` Phase L.
