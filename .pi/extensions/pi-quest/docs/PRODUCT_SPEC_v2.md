# Product Specification — pi-quest v2

**Status**: Accepted draft. Authored 2026-09-04 against pi-quest at
`.pi/extensions/pi-quest` (src ≈ 30,209 LOC, ~110 files; 60 test suites,
~31k LOC).

The driving change: **pi's session transcript is already lossless, and pi-vcc
makes compaction deterministic and zero-LLM. pi-quest therefore stops being a
second database and becomes purely the epistemic workflow enforcer it was
always meant to be — solving only the problems it should solve.**

---

## 1. Position

pi-quest v2 is an **epistemic workflow enforcer** for Pi, not a second
durable store. It keeps the model honest about *what it should do and when*
(research → plan → approve → implement → verify → accept), and deletes every
mechanism whose only job was "the model's summary might forget something".

### 1.1 Why this is possible (measured evidence)

1. **The session file already holds the full transcript and full state.**
   pi-quest today emits the entire quest state as
   `customType:"quest_journal"` entries inside pi's session JSONL. Measured
   on this machine: **15,970 snapshot entries across all session files**;
   **654 session files** for the site project contain them; each file mixes
   `message` entries (the transcript) with `custom` snapshot entries carrying
   ~90 state fields (`active`, `questId`, `stack`, `planConfidence`,
   `saveGeneration`, review state, gate flags, …).

2. **Compaction never touches the file — only the in-context window.**
   pi keeps every session as a JSONL under
   `~/.pi/agent/sessions/<project-dir>/<timestamp>_<id>.jsonl`; compaction
   trims which messages are in context, it does not delete history.

3. **pi-vcc makes compaction deterministic, zero-LLM, ~30–470ms.**
   The "lossy LLM summary dropped my state" failure mode — the entire
   justification for pi-quest's checkpoint/transaction/reconstruction
   machinery — no longer exists. `vcc_recall` (session-scoped) plus snapshot
   parsing make recovery a cheap, deterministic read.

### 1.2 Non-goal

This is **not** a re-scope of the epistemic enforcement features. In
particular `critical_agent/` (~4,599 LOC: plan review, final acceptance,
subagent dispatch, model fallback, single-flight/coalescing) is **untouched**
— it contains ~17 references to "compaction" and none inside the review
files proper; it is behavior, not memory. Reviews still gate implementation
(P2) and completion (P4) exactly as today.

---

## 2. Problems pi-quest v2 SHOULD solve (keep)

These are runtime/enforcement problems, orthogonal to memory. Scope remains
unchanged.

| # | Problem | Mechanism (unchanged) |
|---|---------|------------------------|
| P1 | Agent implements before it understands | Research gate + tool-classification gating (`gates.ts`, `tool_gating.ts`) |
| P2 | Plan drifts from the user's actual request | Requirement capture in draft flow → plan approval (reviewer verdict + user "go") |
| P3 | Contradictory evidence rationalized away | Reassessment triggers on test failure / refinement (`research.ts`) |
| P4 | "Done" claimed before verification | Final acceptance review against the *original recorded request* (`critical_agent/`) |
| P5 | Workflow promises merely advisory | Model-visible enforcement messages + tool-boundary blocks (architectural invariant #1) |
| P6 | Large tasks un-decomposable | Sub-quest hierarchy with parent/child continuity (`subquest.ts`) |

---

## 3. Problems pi-quest v2 should STOP solving (eliminate)

Each entry below is replaced by the "transcript-as-database" model: a durable
snapshot emitted into the session stream on every state transition.

| Eliminated | Current LOC | Replaced by |
|---|---|---|
| Compaction transactions, checkpoints, recovery, resume obligations (`src/compaction/*`) | 2,111 | One invariant: **every state transition emits a snapshot `custom` entry to the session stream.** The latest snapshot is truth; a failed compaction is a non-event. |
| Reconstruction from markdown journals (`src/reconstruction/*`) | 813 | ~150 LOC: parse the current session file for the newest `quest_journal` snapshot; fall back to sibling files. No markdown parsing. |
| Write-master ceremony — `persistence.ts`, `verifyAndMarkSaved`, dirty-fingerprinting, mark-saved gating | ~465 | Snapshot codec (~80 LOC). `quest_mark_saved` becomes a no-op/hidden; "saving" is just emitting a snapshot. |
| Pre-compaction block / steer / checkpoint threads in `hooks/` | ~500 | Deleted. Nothing to block — the last snapshot already survives. |
| Parallel audit database — `logging/*` event schema, `execution.log` write path | 2,168 | ~400 LOC: events ARE `custom` entries in the transcript; `execution.log` becomes a time-ordered renderer over them. |
| Diagnostic/archive packaging — zip, hierarchy parsers, manifest, captured trees | 2,389 | ~600 LOC: render quest view + export the relevant session range. A human artifact, never a source of truth. |
| `draft-prompts.jsonl` verbatim duplicate logs | — | Already in the transcript (full user prompts); file dropped. |

**Estimated reduction: ~7–8k LOC of ~30k (≈25%)** — and critically the
complexity class drops: one storage mechanism, one write path, zero
transactions.

`pi-quest-bundle.zip` (AGENTS.md packaging requirement) survives as a thin
render command over the transcript.

---

## 4. New architecture

```
session JSONL ── the only durable truth (transcript + quest_journal snapshots)
      ▲                             │
 emit snapshot               reconstruct(): parse
 on transition               latest snapshot
      │                             │
      ▼                             ▼
 agent lifecycle ←── [v2 core] ──→ in-memory state
   gates / reviews /                (rebuilt at turn_start when absent)
   subquests /
   enforcement
      │
      ▼
 quest.md      = rendered VIEW (user-facing, read-only contract, never
                 read back as truth)
 future/<qid>.md = draft workspace: agent-authored plan + accumulated requirements (the ONE agent-write surface in drafting, kept)
 execution.log = rendered VIEW
 archive.zip   = rendered artifact (quest view + session range export)
```

- **Agent write master**: every transition emits a snapshot into the current
  session file (a `custom`/`quest_journal` entry, exactly as today — emitting
  this becomes *the* persistence primitive).
- **User write master**: only `future/*.md` drafts. The human edits those by
  hand; approved plans render into the next snapshot reference (hash +
  position), no content duplication.
- **Read path**: `reconstruct()` reads the current session file. Cross-session
  recovery reads sibling session files (section 5). No markdown source.
- **Compaction**: nothing to do — neither block nor checkpoint. pi-vcc
  compacts context; the snapshot survives.

### 4.1 Snapshot contract

A snapshot is the full serializable state (today's ~90-field `StoredState`),
written atomically as one JSONL line tagged `customType:"quest_journal"` with
`questId` and a monotonic `saveCount`/timestamp. Snapshots are emitted:

- at `turn_start`, `turn_end`;
- after any `quest_update_state` / state mutation;
- at draft creation, promotion, approve, archive;
- before long-running operations (subagent launch, review launch).

Recovery = newest snapshot for a `questId`; no more recent snapshot found →
fall back to newest snapshot in sibling files; none → cold start.

---

## 5. Cross-session recall — design decision (research resolved)

**Question**: does "thrive on lossless compaction" survive a *new session /
restart*, given `vcc_recall` is session-scoped?

**Research findings**:

- Session files persist indefinitely under
  `~/.pi/agent/sessions/<project-dir>/<timestamp>_<id>.jsonl`; restarts
  never delete them.
- 654 site-project files already carry `quest_journal` snapshots tagged by
  `questId`, so **cross-session discovery needs no index**: scan sibling
  files newest-first for the latest snapshot matching `questId`. The
  timestamp-prefixed filenames make ordering O(1) without reading every file.
- Search is bounded: skip files newer than the in-memory session, cap the
  scan window (e.g. top-N newest files, then newest-first), and stop at the
  first consistent snapshot.

**Decision**: implement cross-session recovery as a pi-quest-side adapter over
the same JSONL (reusing pi-vcc's load/search primitives where available),
keyed by `questId` + project-dir. `vcc_recall` stays session-scoped for
intra-session history; v2 adds `quest_recover` (project-scoped snapshot
recovery) — exactly the "survive a new session" promise. An upstream pi-vcc
`scope:"project"` is a possible follow-up, not a dependency.

---

## 6. Agent-visible tool surface (v2)

| Tool | Change |
|------|--------|
| `quest_update_state` | Mutates in-memory state + emits snapshot. No markdown write-master contract. |
| `quest_mark_saved` | **Removed** — emitting a snapshot is the persistence primitive; the concept of "unsaved" disappears. |
| `quest_subquest` | Kept. Parent/child links live in the snapshot. |
| `quest_archive` | Render-from-transcript artifact. |
| `quest_recover` | **New** — forces reconstruct-from-transcript; auto-runs at `turn_start` when state is absent. |
| Draft operations | Kept. Agent authors `future/<qid>.md`; snapshot records hash + position only. |
| Review/gating tooling | Kept, unchanged (P1–P6). |

System-prompt workflow rules change accordingly: "read quest.md after
compaction" becomes "state is in the last snapshot; use `vcc_recall` for
history".

---

## 7. Build-up plan (TDD, phased)

Rewrite the persistence surface; keep the workflow. Enforcement stays green
at every step.

1. **Ph0 — golden contract tests.** Freeze today's P1–P6 enforcement
   semantics (gates, reviews, sub-quests, obligations, reassessment,
   validation, tool gating) as green baselines from the existing 60 suites.
   This is the acceptance net for everything that follows.
2. **Ph1 — snapshot store.** Emit-snapshot = persist; add `reconstruct()`;
   delete `compaction/`, `persistence.ts` ceremony, mark-saved gating, and
   the pre-compaction hook threads. Persistence-specific tests are rewritten
   to snapshot semantics; enforcement tests stay green unchanged.
3. **Ph2 — transcript-derived views.** `execution.log` renderer, quest.md
   renderer (read-only contract), archive-thin. Remove the logging event DB
   and diagnostic parsers.
4. **Ph3 — cross-session recovery adapter.** Sibling-file snapshot discovery
   with bounded scan. New tests: restart → new session → `quest_recover`.
5. **Ph4 — pi-vcc interop hardening.** Verify the compaction boundary with
   pi-vcc active: no blocks, no checkpoints, snapshot emitted before
   turn-end; manual + automated compaction both recover.
6. **Ph5 — cut the rest.** Drop `draft-prompts.jsonl`, orphan-compaction
   threads, resume-obligation delivery. Update AGENTS.md workflow rules.
7. Every phase ships the `pi-quest-bundle.zip` render command (its test +
   packaging script become the doc-driven dual of this spec).

Sequencing keeps P1–P6 green at each step, so no workflow capability is ever
at risk while the storage layer is replaced.

---

## 8. Scope decisions (accepted)

- **`critical_agent/` is out of slimming scope.** It is pure enforcement
  (~17 compaction references total, none in review files). Reviews continue
  to gate implementation and completion exactly as today.
- **`quest.md` becomes read-only for the agent and a view for the user.**
  Hand-edits to `quest.md` must not be read back as truth; R3 contract:
  the file header states it is generated output. Without this, user edits
  reintroduce the drift pi-quest was built to stop.
- **The transcript is the source of truth.** `.pi/quest/current/**/quest.md`
  is a render, not a second database.

---

## 9. Risks and open decisions

- **R1 — snapshot growth.** Full-state snapshots accumulate (15,970 now).
  Keep them; prune to last-N per `questId` per project only after v2 is
  battle-tested and pruning is proven safe against recovery.
- **R2 — migration.** Existing `.pi/quest/**` records for pre-snapshot runs
  are read-only. v2 bootstraps from the newest snapshot already on disk
  (present for the site project) and treats older archives as artifacts.
- **R3 — user expectations.** Some users hand-edit `quest.md` today. The
  read-only-view contract must be communicated (file header + workflow rules)
  or drift returns.
- **Open** — none blocking Ph0. Confirm snapshot emit hooks list (§4.1) once
  Ph1 starts.

---

# Part II — Behavioral specification (prescriptive)

**Status: IN PROGRESS — slice 1 of 3 (lifecycle + gates).** Normative
authority is `HIGH_LEVEL.md` (repo root): the three modes below are quoted
in spirit and elaborated here. Where v2 deliberately differs from current
behavior, the delta is marked **V2-DELTA**. Slices 2 (reviews + tools +
commands) and 3 (surfaces + acceptance criteria) follow.

## B0. The product (normative core)

A quest moves through three modes — **drafting**, **implementing**,
**validating** — identically for quests and sub-quests:

- **Drafting:** the agent may write exactly one file (the draft) and
  otherwise only read/research. Every content-changing save cancels any
  in-flight review and boots a fresh adversarial reviewer. Reviewer PASS
  auto-promotes to implementing (recorded research + actionable plan
  required); FAIL returns findings and the agent revises. The user may
  approve at any moment — immediate promotion, reviewer dismissed — but
  approval is never required.
- **Implementing:** unrestricted. Setbacks are recorded with evidence and
  announced; nothing blocks. The agent works from the Exact Next Action
  until it claims completion.
- **Validation:** a validator checks implementation against the plan. PASS
  → slim archive (quest view + session-range reference + manifest). FAIL →
  findings, demoted to implementing.

## B1. Quest lifecycle

### B1.0 Identity (qid everywhere, no slugs)

Every quest — draft or active — is identified solely by its quest id,
assigned once at detection. The id is the base62 encoding (`0-9A-Za-z`,
big-endian, fixed minimum width) of unix epoch seconds at creation (~6
characters today), bumped monotonically on same-second or directory
collision, so ids are short, filesystem-safe, and roughly time-ordered.
The id names all paths (`future/<qid>.md`, `current/<qid>/`,
`archive/<qid>.zip`) and tags all snapshots. There are no slugs: no
slug generation, no slug matching, no slug-correction. Human-readable
names live in the quest's `name` field for display and name matching
only, never in paths.

### B1.1 States

The externally visible lifecycle states are exactly:

| State | Meaning |
|---|---|
| `IDLE` | No active quest. The agent works unconstrained |
| `PROVISIONAL` | A substantive prompt was detected; quest identity not yet established. Implementation blocked |
| `DRAFT` | A future draft exists (`future/<qid>.md`) and is being authored/reviewed. Only the draft file is writable |
| `AWAITING_REVIEW` | A `plan_review` or `final_acceptance` review is running. Draft file stays writable; all other writes blocked |
| `IMPLEMENTATION_ALLOWED` | Gates open; the agent works autonomously from the Exact Next Action |
| (terminal) | `COMPLETED` / `FAILED` / `ABANDONED` — quest archived, see B1.5 |

Reassessment is **not** a lifecycle state: it is a recorded condition
(reason + evidence + model-visible steer) that blocks nothing (B1.7).
**V2-DELTA:** `PRE_COMPACT_DUMP_PENDING`, `COMPACTING`,
`RESEARCH_PENDING`, `CONFIRMATION_PENDING`, and `ACTIVE_CLEAN/ACTIVE_DIRTY`
as save-hygiene states are all deleted. The only dirty distinction that
survives is snapshot-pending (B1.6).

### B1.2 Detection and formation

1. On a substantive user prompt with no active quest or draft, pi-quest MUST
   assign a quest id (B1.0) and create a provisional root quest: record the
   verbatim prompt as `pendingRootRequest`, set round 1, and block
   implementation (`PROVISIONAL`, cf. `initProvisionalRootQuest`).
2. If the prompt names an existing quest (quest-name or quest-id match) or
   an existing future draft (quest-id match), pi-quest MUST resume/adopt it
   instead of creating a new quest.
3. Otherwise pi-quest MUST create `future/<qid>.md` from the prompt, enter
   `DRAFT`, and accumulate subsequent requirements/refinements into the
   draft until a plan exists.
4. A new user request arriving while a quest is active does NOT create a
   second active quest — only one quest is ever active. It is interpreted
   in the context of the active quest (refinement, amendment trigger, or
   sub-quest seed) unless it constitutes a separate quest under the scope
   rules below.

Scope rules (normative): a request is a separate quest iff its objective
and scope cannot be satisfied as an amendment (B1.8) or sub-quest (B1.10)
of the active quest. Borderline cases stay in context: a wrongly-merged
request can later be split out as a sub-quest, while a wrongly-split quest
fragments review unnecessarily — so the bias is to keep.

### B1.3 Draft → active (B2: user "go" override kept)

1. The agent authors the plan by editing the draft file's
   `## Implementation Plan` section. Reviews follow the supersede rule:
   every **content-changing** save cancels any in-flight review for the
   draft and boots a fresh one, so a verdict can never land on stale
   content. Saves that change nothing boot nothing: reviews dedup on the
   draft content hash, already-approved content is not re-reviewed, and
   with no reviewer registered the flow falls back to user approval only.
2. A `FAIL` verdict returns the quest to draft revision with the
   findings; the agent revises the draft and saves, which supersedes and
   re-reviews. Nothing else unblocks drafting.
3. On reviewer `PASS` (with recorded research and an actionable plan) the
   draft is promoted to the active quest automatically, and the agent is
   notified to proceed — promotion is never a question. The agent MAY
   present findings to the user as it proceeds, but no gate waits for an
   answer.
4. **User approval (B2, normative):** an explicit confirmation-classified
   user "go" at any moment in drafting promotes the draft immediately,
   whether or not a review has passed. The pending review MUST be
   cancelled (never orphaned), the approval MUST be logged, and the agent
   MUST proceed from the draft plan as the user's chosen risk. Approval
   binds the agent's critics, never the human: it is always permitted,
   never required.

### B1.4 Active quest

1. The agent works without restriction: no research rounds, no
   confirmation waits, no reassessment blocks. Promotion preconditions
   (recorded research, actionable plan, PASS or user approval) were met at
   the transition; they are never re-checked mid-implementation.
2. Sub-quests follow the same three modes with the same rules; they skip
   nothing and add nothing.
3. The agent works from the quest's **Exact Next Action** — a live pointer
   to the next justified action, updated on every substantive change, never
   a restatement of completed work.
4. User refinements during implementation are recorded and announced, and
   feed the validator at completion — they never block (B1.7).

### B1.5 Completion and archive (B3: simplified)

1. A quest ends as `COMPLETED` (work verified), `FAILED` (work unverifiable
   or blocked), or `ABANDONED` (contradiction recorded, work stopped via
   `quest_archive --abandon`).
2. `COMPLETED` (top-level) requires final acceptance review against the
   **original recorded request** (slice 2 defines the flow).
3. Archiving renders the quest view, writes the run manifest, stores a
   **session-range reference** (session file + entry range — not a full
   export copy), and clears the active state. **V2-DELTA:** no
   `CHANGELOG.md` append, no `compact` parameter, no terminal-commit
   verification ceremony beyond the acceptance verdict.
4. Archived is final: a later user request does not re-open the quest —
   follow-ups are new quests (they may cite the old quest id).

### B1.6 Snapshot pending (replaces dirty/save-generation)

`dirty` is redefined: it means *a state transition has occurred since the
last emitted snapshot*, nothing more. There are no save generations,
hashes, or fingerprints. The flag clears on snapshot emission. Any gate
that reads it (review validity, archive gating) keeps its shape; only the
meaning changes from "quest.md unsaved" to "snapshot not yet emitted".

### B1.7 Reassessment (advisory, B6)

Reassessment is recorded, never blocking: when a tool result reports
meaningful failure, a user refinement arrives, or new evidence contradicts
a recorded assumption, pi-quest MUST record the reason with its evidence
and send a model-visible steer — and the agent carries on. There is no
required resolution ritual, no fresh-investigation mandate, no
un-confirmation. Available responses, all optional: continue with a
recorded conclusion via `quest_update_state`, rebut with evidence via
`quest_rebut`, escalate via `quest_ask_human`, or stop via
`quest_archive --abandon`. Unresolved contradictions are caught by
validation at completion. (The investigation-epoch/receipt/evidence
apparatus is deleted in M1.)

### B1.8 Plan amendments (non-destructive, in-scope only)

During implementing, the agent records plan changes via
`quest_update_state` with `plan` + `planRevisions` stating *what changed,
what evidence forced it, and what was considered and rejected*. The
approved v1 text is never rewritten — amendments append as v2, v3…, each
emitting a snapshot. No gate and no review fire at amendment time.

Scope boundary (normative): **an amendment adjusts the plan toward reality;
it never changes the quest's scope.** A discovery that redefines the goal
is not a bigger amendment — it is a new draft/quest, with the current one
completed as-is or abandoned. Final acceptance therefore judges two
things: (a) implementation against the *latest* plan version, and (b) each
amendment's appropriateness *and* in-scope-ness against the original
request. A PASS binds to `(planVersion, snapshot)`; any later amendment
invalidates it.

### B1.9 Human absence (never waits)

No quest progress may ever depend on a human response — no gate, tool, or
flow waits indefinitely for the user. `quest_ask_human` records the
question with the agent's recommended default, notifies, and asks with a
timeout signal: one minute by default, configurable per
question (no wait, any duration, or indefinite). On timeout the agent
proceeds with the default; the default is recorded snapshot-side and the
validator judges it like any amendment. A late user answer is applied
retroactively via classification (confirmation or refinement). Cancelled
or unavailable prompts count as absence, never as silent no-ops. There is
no end-of-quest choice prompt: validation PASS archives automatically
(B1.5).

### B1.10 Sub-quests

Terminology (normative): **child = sub-quest, always.** Reviewer
subprocesses are called reviewers, never children.

1. **Phases vs. sub-quests.** Phases are sequential stages inside one
   quest's plan (checklist + per-stage verification, no separate
   lifecycle). Sub-quests are full-lifecycle units with their own quest
   id, used only for complex sub-tasks.
2. **Decomposition.** The agent proposes the decomposition during
   drafting/research as part of the plan, linked as `[[<qid>]]`. The plan
   reviewer judges it: genuine boundaries, complete coverage, no
   artificial fragmentation, and per-child acceptance criteria written
   upfront.
3. **Child lifecycle (light).** Each sub-quest implements and validates
   mandatorily; it is drafted and reviewed only if it deviates from its
   brief, in which case the deviation is a parent amendment (B1.8).
4. **Single focus.** One quest is active at a time (the stack top). A
   parent waits while a child runs; "parallel" sub-quests are an ordered
   backlog, not concurrent execution.
5. **Returns.** A completing child yields a findings summary. The parent
   MUST record it and either amend (if affected) or explicitly continue
   past it. The parent validator checks every child summary was addressed.
6. **Failure isolation.** A failed or abandoned child does not fail its
   parent. The parent records, adjusts (alternate approach or scoped-down
   acceptance), and the validator judges; contradictions surface in parent
   validation, never silently.
7. **Completion gating.** A parent cannot claim completion while any child
   is non-terminal. Validation runs bottom-up: children validate first;
   parent validation judges integration, completeness, children's outcomes
   and amendments — never re-validating child work itself.
8. **Depth cap (`TUNABLE`, default 3).** Quest → sub → sub-sub; deeper
   requires explicit justification. Fan-out and review counts are advisory
   steers, not hard blocks.
9. **Tree recovery.** `quest_recover` restores the whole tree by following
   parent/child qid links across snapshots, not just the single quest.

## B2. Implementation gates (truth table)

Gate evaluation order is normative — first match wins. Anything not in
this table never blocks, notably: reassessment (advisory, B1.7), research
standing (a promotion precondition, B1.3 — never re-checked
mid-implementation), and user confirmation (no such gate exists, B2/B7):

| # | Condition | State name | Code | Required agent action |
|---|---|---|---|---|
| 1 | Caller is a reviewer session | `REVIEWER_READ_ONLY` | `IMPLEMENTATION_BLOCKED` | Read/search only; report via verdict |
| 2 | Provisional root, identity unestablished | `PROVISIONAL_RESEARCH_PENDING` | `RESEARCH_REQUIRED` | Investigate, establish quest identity, call `quest_update_state` with findings |
| 3 | Active draft with outstanding revision | `DRAFT_REVISION_PENDING` | `DRAFT_REVIEW_REQUIRED` | Edit the draft plan to address findings; a content-changing save supersedes any in-flight review and boots a fresh one |
| 4 | Active draft, plan not yet authored | `DRAFT_PENDING` | `DRAFT_REVIEW_REQUIRED` | Author `## Implementation Plan` in the draft file |
| 5 | No active quest | `IDLE` | — (not blocked) | — |
| 6 | Pending sub-quest continuation inconsistent with active state | `PENDING_RESUME_INCONSISTENT` | `PENDING_RESUME_INCONSISTENT` | Reconcile parent/child hierarchy first |
| 7 | `plan_review`/`final_acceptance` running | `AWAITING_REVIEW` | `PLAN_REVIEW_REQUIRED` | No writes until verdict — except the draft file (exemption below); reads allowed |
| 8 | Otherwise | `IMPLEMENTATION_ALLOWED` | — (not blocked) | Work autonomously from Exact Next Action |

**V2-DELTA rows removed:** `RESUME_STATE_INCONSISTENT`,
`COMPACTION_TRANSACTION_FAILED`, `COMPACTION_IN_PROGRESS` (Tier 1 — no
transactions exist); `REASSESSMENT_PENDING`, `RESEARCH_PENDING`,
`CONFIRMATION_PENDING`, `PLAN_REVIEW_PENDING` as standing gates (B6+B7 —
reassessment is advisory, research is a promotion precondition, no
confirmation gate exists; plan-review validity is checked at promotion,
slice 2). Reviewers (row 1) stay read-only: a reviewer session can
never attain implementation permission on the main quest.

### B2.1 Tool classes

**Draft-file exemption (normative, highest precedence):** writes via
`edit`/`write` (and `user_` variants) targeting `future/<activeDraft>.md`
are exempt from every gate in every state — including `DRAFT_PENDING`,
`DRAFT_REVISION_PENDING`, and `AWAITING_REVIEW` while a review runs.
Authoring or revising the plan proposal is never implementation. In DRAFT
states the draft file is the **sole** writable path: all other mutating
tools stay blocked.

Blocked = mutating tools: `edit`/`write` (and `user_` variants) to any
other path, mutating
`bash` (anything that is not a read-only probe; command chains split and
each segment classified; file redirection counts as mutation),
`subagent` launch, background-task mutation, and direct writes to quest
paths (blocked before execution — the agent MUST use `quest_update_state`).
Always allowed: reads, searches, non-mutating bash, interaction tools
(questions), and journal ops needed to resolve the block itself
(`quest_update_state`, plus `quest_rebut`/`quest_ask_human` even inside a
running critical review). Every block MUST produce a model-visible message
with the state name, the stable error code, and the required next action
(architectural invariant #1); UI notifications alone are never sufficient.

### B2.2 Gate-open directives

Transitions into `IMPLEMENTATION_ALLOWED` MUST announce themselves to the
model exactly once per transition (deduped on state key): promotion
(draft PASS or user approval) and reassessment-free continuation direct
the agent to proceed autonomously from the Exact Next Action — never to
repeat the previously blocked action blindly if the plan changed. The
agent MAY notify the user of findings as it proceeds; no directive waits
for an answer.