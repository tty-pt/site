---
name: quest-journal
description: "Quest Journal v2: drafting / implementing / validating modes per quest. Main agent only — reviewers receive the review brief, never this skill."
---

# Quest Journal v2 — main-agent workflow rules

You work inside quests. One quest is active at a time. Quest state lives in
the session transcript as `quest_journal` snapshots; `quest.md` files are
generated read-only views, never truth. Every quest has a short alphanumeric
quest id (`future/<qid>.md`); there are no slugs.

## Modes

- **Drafting.** The only write path, in order: (1) `quest_update_state`
  with findings to establish identity; (2) `quest_update_state` with
  `draftName` — the draft lives at `.pi/quest/future/<qid>.md` (its
  scaffold is pre-created when the quest starts, so never create
  directories yourself);
  (3) author the plan: EITHER call the `edit` tool with the draft file path
  (`{ "path": ".pi/quest/future/<qid>.md", "edits": [{ "oldText": "…",
  "newText": "…" }] }`) OR pass the whole plan body as
  `quest_update_state { "plan": "…" }` — the runtime splices it into the
  draft's `## Implementation Plan` for you. Every other write is
  blocked with the reason and the required next action. While a review runs,
  end your turn: the verdict arrives as a new turn. Research and read
  freely — searches, inspections, and test runs are never blocked. Structure
  the draft with `## Requirements`,
  `## Evidence`, and `## Implementation Plan` sections. Every
  content-changing save cancels the running review and boots a fresh
  adversarial reviewer. Reviewer verdicts are PASS or FAIL with findings.
  FAIL returns findings — revise the plan and save. PASS promotes you to
  implementing automatically (recorded research + actionable plan required).
  The user may reply `go` at any moment to promote immediately; approval is
  never required. Rebut a verdict with evidence via `quest_rebut`.
  `future/<qid>.md` stays the one quest document through implementing and
  validating; it is removed only when the quest archives.
- **Drafting to pass review in one pass.** Every save boots a fresh
  adversarial reviewer, so build up to the maturity bar before you save.
  The reviewer checks your *plan* against the recorded request along these
  dimensions: does it address the objective; are requirements omitted; did
  it substitute a different problem; is research sufficient; are assumptions
  verified; does the work sequence fit; is complexity unnecessary; was an
  alternative dismissed without evidence; does it commit prematurely; does it
  contradict itself; does it credibly satisfy the request. Draft so the
  answers are visibly yes:
  - Write requirements as numbered, objective-stated bullets. A draft clears
    the review threshold at **2 requirements**, or **1 requirement + 7
    evidence items**, with an actionable plan — below that, the reviewer
    fails fast.
  - Distinguish a **user requirement** (blocks), a **technical constraint**
    (binds your approach), and a **reviewer preference** (never blocks).
    Each requirement needs evidence or a verification step; research you
    file via `refinement` lands as `## Findings (pre-draft investigation)`
    and counts as evidence.
  - Cite code-generation claims as `file:line` so the save-time claims check
    stays silent; the reviewer spot-checks named lines rather than
    re-auditing the tree.
  - **Preview before you pay:** call `quest_update_state` with
    `{ "checkPlan": "<plan body>" }` to read the deterministic draft profile
    (counts, maturity-bar verdict, citation resolution) without writing or
    booting a review. Iterate on the profile until it reads `maturity bar:
    met`, then save once.
  - Revise from the verbatim FAIL text the wake carries; a FAIL marks
    findings outstanding until your next save supersedes them.
- **Implementing.** Unrestricted. Record setbacks with evidence as they
  happen; nothing blocks. When reality contradicts the plan, record an
  amendment with reasons via `quest_update_state` — amendments adjust the
  plan, never the scope (scope change = new quest). Work from the quest's
  Exact Next Action. User refinements are recorded and feed the validator.
  When done, claim completion via `quest_update_state` with `claimComplete`
  — an unverified claim that boots the validator immediately. The quest doc
  is locked to direct edits during implementing; all plan changes go through
  `planRevision` (peer-reviewed) and completion through `claimComplete`. The
  doc's `## Status` section tracks the phase but never marks completion as
  true on your say-so.
- **Validation.** A validator checks the implementation against the approved
  plan plus amendments. PASS: run `quest_archive` to finish (agent quest doc
  + rendered view + manifest). FAIL: findings return you to implementing and
  the doc's `## Status` is reset, so a fresh claim re-arms the trigger.
  Archived is final — follow-ups are new quests citing the old id.

## Tools

- `quest_update_state` — your write path: findings, drafts, amendments,
  next action, completion claims. Emitting a snapshot IS persisting; there
  is no save step and no `quest_mark_saved`.
- `quest_subquest` — spawn a linked sub-quest for a complex sub-task. The
  parent waits; you resume on return with findings. Depth cap 3. A failed
  child never fails its parent — record, adjust, continue.
- `quest_archive` — finish as completed (needs a current validation PASS;
  claim via `quest_update_state` with `claimComplete` first), failed, or
  abandoned. Archiving unvalidated work as failed/abandoned requires
  `confirmDiscard:true` plus a summary of what was discarded and why.
  Archive zips the quest doc `future/<qid>.md` plus the rendered view and
  manifest into `archive/<qid>.zip` (sole artifact) and removes both sources.
- `quest_recover` — rebuild state from the transcript, including earlier
  sessions. Runs automatically when state is absent.
- `quest_rebut` — answer a review with evidence; a successful rebuttal
  reopens the question.
- `quest_ask_human` — ask with a recommended default and timeout (one
  minute default). It dispatches through question providers — tools with a
  working invocation protocol first, the built-in input prompt as the
  guaranteed fallback. Never blocks: absence, cancellation, or timeout
  proceeds with the default, and a late answer still applies when it
  arrives.

## Commands

`/quest` (resume by qid/name, show the active quest, or pick from current quests when idle — fresh sessions start with no active quest), `[^Q]` (floating plan viewer), `/quests` (list), `/quest-del` (archive/kill).
Everything else the system does by itself.

## Durability

State is stamped into the transcript on every change and re-read before
every reply: you survive compaction and restarts. If state is ever absent,
call `quest_recover`. Never hand-edit generated quest views.
