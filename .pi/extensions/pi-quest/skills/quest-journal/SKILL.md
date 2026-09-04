---
name: quest-journal
description: "Quest Journal v2: drafting / implementing / validating modes per quest. Main agent only — reviewers receive the review brief, never this skill."
---

# Quest Journal v2 — main-agent workflow rules

You work inside quests. One quest is active at a time. Quest state lives in
the session transcript as `quest_journal` snapshots; `quest.md` files are
generated read-only views, never truth. Every quest has a short alphanumeric
quest id (`future/<qid>.md`, `current/<qid>/`); there are no slugs.

## Modes

- **Drafting.** You may write exactly one file: the draft (`future/<qid>.md`).
  Every other write is blocked with the reason and the required next action.
  Research and read freely. Structure the draft with `## Requirements`,
  `## Evidence`, and `## Implementation Plan` sections. Every
  content-changing save cancels the running review and boots a fresh
  adversarial reviewer. Reviewer verdicts are PASS or FAIL with findings.
  FAIL returns findings — revise the plan and save. PASS promotes you to
  implementing automatically (recorded research + actionable plan required).
  The user may reply `go` at any moment to promote immediately; approval is
  never required. Rebut a verdict with evidence via `quest_rebut`.
- **Implementing.** Unrestricted. Record setbacks with evidence as they
  happen; nothing blocks. When reality contradicts the plan, record an
  amendment with reasons via `quest_update_state` — amendments adjust the
  plan, never the scope (scope change = new quest). Work from the quest's
  Exact Next Action. User refinements are recorded and feed the validator.
  When done, claim completion via `quest_update_state` with `claimComplete`.
- **Validation.** A validator checks the implementation against the approved
  plan plus amendments. PASS: run `quest_archive` to finish (quest view +
  session reference + manifest). FAIL: findings return you to implementing.
  Archived is final — follow-ups are new quests citing the old id.

## Tools

- `quest_update_state` — your write path: findings, drafts, amendments,
  next action, completion claims. Emitting a snapshot IS persisting; there
  is no save step and no `quest_mark_saved`.
- `quest_subquest` — spawn a linked sub-quest for a complex sub-task. The
  parent waits; you resume on return with findings. Depth cap 3. A failed
  child never fails its parent — record, adjust, continue.
- `quest_archive` — finish as completed (needs a current validation PASS),
  failed, or abandoned.
- `quest_recover` — rebuild state from the transcript, including earlier
  sessions. Runs automatically when state is absent.
- `quest_rebut` — answer a review with evidence; a successful rebuttal
  reopens the question.
- `quest_ask_human` — ask with a recommended default and timeout (one
  minute default). Never blocks: absence, cancellation, or timeout proceeds
  with the default, and a late answer still applies when it arrives.

## Commands

`/quest` (resume or show), `/quests` (list), `/quest-del` (archive/kill).
Everything else the system does by itself.

## Durability

State is stamped into the transcript on every change and re-read before
every reply: you survive compaction and restarts. If state is ever absent,
call `quest_recover`. Never hand-edit generated quest views.
