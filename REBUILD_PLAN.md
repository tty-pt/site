# pi-quest v2 — Build Plan (from-scratch, layered)

**Status: PLANNED, NOT STARTED.** Nothing has been executed yet; the working
tree is untouched.

Approach: **from-scratch rebuild** at `.pi/extensions/pi-quest/`, with v1
moved out to an archive directory as a read-only oracle (reference only,
never source; exact location TBD — see §12). The new version is built
simpler, under a **layered architecture with a pure core** (§5), against
`HIGH_LEVEL.md` as the normative product spec and `PRODUCT_SPEC_v2.md`
Part II as the behavioral elaboration. This supersedes the in-place
modification approach (recorded with reasons in Appendix B); the
in-place analysis survives where still true (discard set §3, B-decisions
§2, risk evidence §11).

- Baseline: v1 src 30,233 LOC; tests 31,290 LOC / 54 files. Target: ~5–6k
  LOC where every file traces to a HIGH_LEVEL sentence.
- Design authorities: `HIGH_LEVEL.md` (product, normative),
  `.pi/extensions/pi-quest/docs/PRODUCT_SPEC_v2.md` Part II (behavior).

---

## 1. Premises (evidenced, 2026-09-04)

pi-quest already serializes complete state into the session JSONL as
`quest_journal` snapshots (15,970 entries measured; 654 site-project session
files), and those entries are `type: "custom"/"custom_message"` — invisible
to vcc's summarizer (which only collects `type === "message"`), so they cost
zero summary tokens and survive every compaction on disk. **P-vcc:** vcc is
currently dormant (`overrideDefaultCompaction: false`); per D8 it is
enabled in staged fashion, but the memory guarantee never rests on vcc's
lossy summary — it rests on **per-turn `reconstruct()` from the newest
on-disk snapshot plus system-prompt injection** (the proven
`before_agent_start` mechanism), which no compactor can erase.
**P-comp (PROVEN):** session-file scan (first 80 site files) found 33
`compaction` entries, all appended with `firstKeptEntryId` + `summary` +
`parentId`, full prior history intact — the transcript is append-only under
*both* compactors. The v1 compaction transactions, mark-saved ceremony,
markdown reconstruction, and parallel event DB therefore have no
corresponding HIGH_LEVEL requirement and are not rebuilt; the enforcement
behavior (gates, reviews, lifecycle) is reimplemented clean against the
spec, not ported.

---

## 2. Decisions (locked)

| # | Decision | Value |
|---|----------|-------|
| D1'' | Approach | From-scratch rebuild, layered architecture (§5). v1 is oracle only |
| D2 | Cross-session recall | `quest_recover`: newest-snapshot scan over sibling session files (`~/.pi/agent/sessions/<project-dir>/`). No index; no upstream pi-vcc dependency |
| D3 | Reviewer internals | Out of scope **except** the verdict-vocabulary prompt rewrite (B5): the reviewer prompt teaches PASS/FAIL only; policy stays tolerant of `APPROVE` |
| D4'' | Location | v1 moves to `.pi/quest/archive/pi-quest-v1/` (directory, git-tracked, greppable oracle; pi never loads outside `.pi/extensions/`); v2 is developed at `.pi/extensions/pi-quest/` (slot reused; site `AGENTS.md` zip path keeps working) |
| D5'' | Swap strategy | **Immediate** (locked): v1 vacates and the v2 skeleton takes the slot at S0; every pi session live-tests from then on — resilience rule holds (loadable-and-graceful at every commit) |
| D6'' | No-v1-copy rule | Zero v1 source lines in v2. v1 behavior informs spec + tests, never implementation |
| D7 | Persistence contract | One write path: emit a full-state `quest_journal` snapshot on every transition. Latest snapshot is truth; a failed compaction is a non-event. `quest.md` is a generated read-only view. No `quest_mark_saved` |
| D8 | Compactor | **Enable vcc override, staged**: M0–S5 use a repo-checked-in `pi-vcc-config.override.json` via `PI_VCC_CONFIG_PATH` (zero effect outside those shells); global flip of `~/.pi/agent/pi-vcc-config.json` only at completion, with before/after recorded |

Architecture decisions (locked):

- **A1 — reducer.** Every pi event flows `hooks/tools → reduce(state, event)
  → {state, effects[]} → interpreter`. Domain is pure; the interpreter is
  the single impure choke point.
- **A2 — review transport behind an interface.** `ReviewRunner {
  launch(spec, signal): Promise<Verdict> }`; default impl uses the
  `subagent` tool with the proven AbortController + cancel-bridge abort
  pattern. ~30 lines of interface decouple domain from pi internals.
- **A3 — wordings in `messaging/`.** All model/user-facing texts live as
  reviewable prose, referenced by key from domain decisions (never inline
  strings in logic).

Behavior decisions (locked, feed the spec):

- **B1 — economy commands deleted** (`/quest-economy`, `/quest-warning`,
  `/quest-subquest-threshold`, pressure thresholds). Dead knobs.
- **B2 — user approval kept as standing permission, never a requirement.**
  Explicit "go" anytime in drafting promotes immediately; pending review
  cancelled (never orphaned); approval logged. No confirmation gate exists
  anywhere.
- **B3 — archive simplified.** Quest view render + session-range reference
  (file + entry range, not a full export copy) + run manifest. No
  `CHANGELOG.md` append, no `compact` param. `abandon` stays.
- **B4 — draft auto-review thresholds frozen (`TUNABLE`).** ≥2
  requirements, or ≥1 + ≥7 evidence items, and only with an actionable
  plan draft in the file.
- **B5 — verdicts PASS/FAIL only** (see D3).
- **B6 — reassessment advisory, never blocking.** Record reason + evidence
  + steer; agent carries on. No epochs, receipts, un-confirmation, or
  review suppression.
- **B7 — research + confirmation fold into promotion; no confirmation gate.**
  Promotion requires recorded research + actionable plan + (reviewer PASS
  or user go). The agent may ask via `quest_ask_human`; no gate waits.
- **B8 — `HIGH_LEVEL.md` normative core.** Its three modes open the
  behavioral spec as §B0; Part II sections elaborate them.
- **B9 — plan-amendment semantics.** `quest_update_state` keeps `plan` +
  `planRevisions` under a non-destructive rule (v1 text never rewritten;
  amendments append with what/evidence/rejected alternatives; no gate at
  amendment time). Amendments are in-scope reality-adjustments by
  definition — scope change means a new quest. Final-acceptance PASS binds
  to `(planVersion, snapshot)`.
- **B10 — qid everywhere, short ids, no slugs.** Identity assigned once at
  detection; base62 unix-seconds (~6 chars), monotonic bump on collision;
  names all paths (`future/<qid>.md`, `current/<qid>/`,
  `archive/<qid>.zip`) and tags all snapshots. Slug generation, slug
  matching,   and slug-correction are deleted, not ported.
- **B11 — human absence never blocks.** No gate, tool, or flow waits
  indefinitely for the user. `quest_ask_human` records question +
  recommended default, notifies, and races the harness ask against a
  timeout (one minute default; per-question configurable to no-wait, any
  duration, or indefinite); on timeout the agent proceeds with the
  recorded default, judged at validation like any amendment. Late answers
   apply retroactively via classification; cancelled/unavailable counts as
   absence. No end-of-quest choice prompt (PASS archives automatically).
- **B12 — sub-quests.** Phases are stages-in-plan; sub-quests are
  full-lifecycle units for complex sub-tasks only. Light child lifecycle
  (implement→validate mandatory, review on deviation); single focus
  (ordered backlog, no concurrency); return protocol (record + amend or
  explicitly continue); failure isolation (child failure never fails
  parent); completion gating (no parent completion with non-terminal
  children; bottom-up validation of integration, not re-validation);
  `[[<qid>]]` links with qid stack; depth cap 3 default (`TUNABLE`);
  tree recovery via qid links. Terminology: child = sub-quest, always
  (reviewers are never "children").
- **B13 — three commands only.** `/quest` (resume/show), `/quests`
  (inspect, absorbs status), `/quest-del` (kill switch). Dropped as
  redundant with autonomy: promote (chat "go"), refine (chat
  classification), draft (auto-detection), subquest-set (agent-side),
  status (merged), recover (automatic + `/quest` covers manual entry).
- **B14 — plugin bindings (override-only).** Peer tools plug in via
  `.pi/settings.json` `"pi-quest"."interfaces"` (`asking.tool`,
  `reviewRunner.tool`; defaults `ask_questions`/`subagent`). Declared
  binding wins when the tool exists, else built-in defaults, else the
  specified degradation. One resolution point (kills the hardcoded-name
  scatter); unknown names or missing tools warn visibly, never crash.
  Timeouts use the transport abort signal, not just a race. Code-level
  runner injection stays outside JSON. Reverse-direction advertisement
  (peers declaring what they provide) is a named future, not built.

Readability decisions (the main file reads like the high-level plan):

- **RD1 — machine-checked spec mapping.** `scripts/check-spec-map` runs
  with the DAG/complexity gates from S0: every HIGH_LEVEL `#section` must
  be referenced by ≥1 `// HIGH_LEVEL: #section` code tag, and every tag
  must name an existing section. Violations fail the build.
- **RD2 — budgets fail-closed.** File <350 / function <80 LOC are hard
  failures (no v1-style warns; fresh tree, nothing grandfathered).
- **RD3 — both tag levels, fixed format.** Behavior blocks open with
  `// HIGH_LEVEL: #section` (why) + `// SPEC: Bx.y` (exactly what); the
  format is part of what the map script parses.

Kept problems (unchanged scope): P1 research gate (as promotion
precondition), P2 plan approval vs. the user's request, P3 reassessment as
advisory record, P4 final acceptance vs. the original recorded request, P5
enforceable workflow promises (model-visible), P6 sub-quest decomposition.

---

## 3. What is not rebuilt (from the vcc-active analysis)

| Not rebuilt | v1 LOC | v2 equivalent |
|---|---|---|
| `src/compaction/*` + compact hooks/threads + economy commands | ~2,700 | Nothing (boundary is a non-event) |
| `persistence.ts` ceremony + `quest_mark_saved` | ~465 | Snapshot codec + emit (`durability/snapshots.ts`); `quest_recover` added |
| `src/reconstruction/*` (markdown → state) | ~813 | `reconstruct()`: newest snapshot from branch/file (~150 LOC) + sibling fallback |
| `src/logging/*` event DB + per-turn volume; `src/diagnostic/*` packaging; `draft-prompts.jsonl` | ~4,600 | Minimal append + transcript renderers (`views/`); B3 slim archive |

---

## 4. What is reimplemented (behavior kept, code new)

- Three modes per HIGH_LEVEL §B0; 8-row gate truth table (spec B2);
  draft exemption + supersede rules; PASS auto-promote + anytime user
  approval; advisory setbacks; append-only amendments; dual-scope
  validation with demotion; B3 slim archive.
- Per-turn injection (awareness + rules into system prompt); snapshot
  emission on every transition; `quest_update_state` / `quest_subquest` /
  `quest_archive` / `quest_recover` / `quest_rebut` / `quest_ask_human`;
  sub-quest stack + parent/child links + returns; status bar + entry
  renderer; skill via package manifest (no self-install copy); bundle zip.

---

## 5. v2 architecture (layered, from-scratch)

```
src/
  index.ts          composition root ONLY: 7 calls in HIGH_LEVEL order, zero comments
  drafting/         index (facade) · gate.ts (tool_call + draft exemption — drafting-only)
  implementing/     index (facade; S3 advisory + amendments wire here)
  validation/       index (facade; S3 validator flow wires here)
  subquests/        index (facade; S3 returns + completion gating wire here)
  absence/          index (facade; S3 late-answer wiring lands here)
  durability/       index (facade) · snapshots.ts (codec + caps + emit + reconstruct + recover)
                  · injection.ts (before_agent_start prompt build)
  surface/          index (facade) · tools/index.ts (six tools + registration, S3)
                  · commands/index.ts (three commands, S3)
  hooks/
    events.ts       shared subscriptions (turns, draft-edit, setbacks, claims, returns, user msgs)
  domain/           pure — no pi imports (DAG-enforced)
    quest.ts        state type + transitions (detect/draft/promote/demote/archive/amend)
    gates.ts        truth table: (state) -> Decision (S1)
    reviews.ts      (state, verdict) -> Transition; supersede + hash-dedup (S1 pure rules)
    policy.ts       promotion preconditions, amendment validity, B4 constants (S1)
    effects.ts      Effect ADT + reduce(state, event) -> {state, effects[]} (S1)
  app/
    interpreter.ts  executes effects (ONLY impure choke point besides adapters) (S1)
  review/
    tracker.ts      single-flight + cancel/supersede (drafting today; validation runs in S2/S3)
    runner.ts       implements ReviewRunner (launch + abort bridge) (S2)
    prompts.ts      PASS/FAIL prompt building (plan + validation) (S2)
    verdicts.ts     verdict parsing (S2)
  messaging/        steer/message templates as prose + send wrapper (S3)
  views/            quest.md · manifest · slim-archive renderers (no execution.log — dropped, transcript suffices) (S3)
  ui/               status bar + entry renderer (S3)
  utils/            classifiers (tool/bash), mutex (file mutex kept for same-repo concurrent sessions), files (S1 minimal classifier first)
```

GOLDEN RULE (placement): never put a file or directory in a parent
directory if it is only used in one child — single-use modules live
inside their sole user (`drafting/gate.ts`, `durability/snapshots.ts`,
`durability/injection.ts`, `surface/tools/`, `surface/commands/`).
Shared-by-many stays shared (`hooks/events.ts`, `domain/`, `review/`
— tracker serves validation runs as well as drafting).

Conventions carried over (purposes, not code): every area is a directory
with `index.ts`, imported bare (`./surface`); `tests/` mirroring `src/`;
`scripts/` names (`dag`, `complexity`, `zip`) so existing references keep
working. No side-by-side `area.ts` barrels (v1 had both; v2 picks one). Vertical
facades: `src/{drafting,implementing,validation,subquests,absence,
durability,surface}.ts` — one installer each, called in HIGH_LEVEL order
from a comment-free `src/index.ts`; thin entry points over the layers,
nothing imports a facade except the main file. DAG script
additionally asserts `domain/` imports neither pi packages nor adapter
dirs. Closed effect set: `Block`, `Steer`, `LaunchReview`,
`CancelReview`, `EmitSnapshot`, `Render`, `Promote`, `Archive`,
`NotifyUI` — a tenth effect is a design review, not a patch. Bulk of
tests is `tests/domain/*` (pure, no pi mocks); boundaries tested against
fakes of `ReviewRunner` and the store.

---

## 6. Phase S0 — Preserve v1, vacate slot, scaffold (prerequisite)

> Requires one git commit (WIP preservation + tag + move). Normal
> no-commit rule suspended for S0 only.

1. Run v1 full suite, record green baseline (oracle health check).
2. Commit v1 WIP (uncommitted edits: `src/classification.ts`,
   `src/critical_agent/*`, `src/hooks/handlers.ts`, `src/hooks/index.ts`,
   `src/messaging.ts`, `tests/plan_review.test.ts`; untracked: `STATUS.md`,
   `deno.lock`, `.pi/` quest dir, `docs/PRODUCT_SPEC_v2.md`, site `BLOCK.md`/
   `CHANGELOG.md`/`FINDINGS.md`/`KNOW_NOW.md`/`TEST-PROMPT.md`) + tag
   `pi-quest-v1-baseline`.
3. Move v1 out of the slot: `git mv .pi/extensions/pi-quest` →
   `.pi/quest/archive/pi-quest-v1/` + untracked files alongside (v1 archived
   as an artifact among quest archives; pi auto-discovery stops loading v1
   — global `packages` does not list it). Delete working state with no
   retrocompat: `.pi/quest/current/` + `.pi/quest/future/` contents go;
  `archive/*.zip` retained as read-only history. Stale workspace skill copy
  (`.pi/skills/quest-journal/`, v1 text) deleted — the skill loads via the
  package manifest only.
4. Stage D8: check in `pi-vcc-config.override.json`; quest-work sessions run
   with `PI_VCC_CONFIG_PATH` pointed at it.
5. Scaffold v2 at the slot immediately (D5'' immediate swap): manifest,
   `index.ts` wiring, `deno test` harness, DAG/complexity/spec-map/zip
   scripts (re-derived, RD1–RD3 enforced from day one), `AGENTS.md`
   (with readability contract), `HIGH_LEVEL.md` (carried: Part 1 frozen),
   `docs/` (spec carried). From this point every pi session live-tests v2 —
   resilience rule holds (loadable-and-graceful at every commit).

## 7. Phase S1 — Domain core + gate (slice 1)

- `domain/` (quest, gates, reviews, policy, effects) + `app/interpreter.ts`
  + `drafting/gate.ts` (tool_call + draft exemption) + `durability/snapshots.ts`
  (codec + emit + reconstruct).
- Tests: gate truth table (8 rows), exemption (draft writable under every
  gate incl. mid-review), codec round-trip + caps, reconstruct-from-branch,
  reduce() transition coverage. All pure except interpreter/store fakes.
- Proof: unit-green + live sessions from day one showing blocks +
  exemption text (immediate swap per D5'' — the slot runs the skeleton;
  deno harness alongside for fast iteration).

## 8. Phase S2 — Draft review loop (slice 2, riskiest mechanism first)

- `review/` (transport + prompts + verdicts + tracker) + draft-edit
  detection (`hooks/events.ts`) + B4 thresholds + go-override (B2) +
  review-communication protocol per HIGH_LEVEL (brief/result schemas,
  staleness + invalidation, independence + minimal-info, validator
  protocol, no-mutation list).
- Tests: content-hash dedup (identical save boots nothing), cancel-then-
  trigger supersede, no-reviewer fallback to user path, already-approved
  short-circuit, go-override cancels + logs + promotes.
- Proof: live draft → edit → review boots → edit mid-review → first
  cancelled, second verdicts → PASS auto-promotes; user "go" mid-review
  promotes immediately.

## 9. Phase S3 — Implementing + validation + archive (slice 3)

- Amendments (B9 params + rule) + advisory setbacks (record + steer, no
  block) + ask-human timeout/default/late-answer semantics (B11) +
  sub-quests (B12: light lifecycle, single focus, returns, isolation,
  completion gating, depth cap, tree recovery) + `quest_recover` sibling scan
  + validation flow (dual scope per HIGH_LEVEL) + demote + B3 slim archive
  + remaining surface tools/commands + `messaging/` wordings + `views/` + `ui/` +
  skill + injection.
- Tests: amendment append-only + PASS binding/invalidation, advisory
  non-blocking, recover cross-session, archive shape (view + range ref +
  manifest, no CHANGELOG, no compact param).
- Proof: end-to-end quest live (draft → PASS → implement → amend →
  claim → validate → archive), plus FAIL → demote → continue.

## 10. Phase S4 — Verification, docs, swap (slice 4)

- Full suite green; DAG (incl. domain-purity) + complexity gates pass;
  bundle zip renders; non-quest control session under the override (R-d
  evidence); global D8 flip with before/after recorded.
- Docs (§11 in the struck in-place draft: 10 items — AGENTS.md, README.md,
  ARCHITECTURE_MAP rewrite, PRODUCT_SPEC status, skill ×2, APPEND_SYSTEM,
  site AGENTS.md, STATUS.md, this file).
- Slot swap per §12 decision; v1 archive retained (history + tag keep it
  regardless).

---

## 11. Definition of done

- v2 lives at `.pi/extensions/pi-quest/`, from-scratch, zero v1 source;
  ~5–6k LOC; every file traces to a HIGH_LEVEL sentence.
- Three modes live in pi: drafting (one writable file, supersede reviews,
  PASS auto-promotes, anytime go), implementing (unrestricted, advisory
  setbacks, append-only amendments), validation (dual-scope, demote on
  FAIL, B3 archive on PASS).
- `quest_recover` proven across sessions; compaction (either compactor) a
  non-event; vcc override global per D8 staging.
- Suite green (mostly pure domain tests), DAG + complexity + spec-map
  pass, bundle renders, all 10 doc items updated.

## 12. Risks & open decisions

- **R-a — contract drift (replaces in-place R-a/R-c).** From-scratch
  behavior must match the spec, and the spec must match intent. Mitigation:
  spec slices approved before code slices; HIGH_LEVEL first half frozen;
  v1 oracle consulted for edge cases (dedup keys, exemption paths, cancel
  wiring); M5-style new tests per slice (S1–S3 proofs).
- **R-b — refought edge cases.** v1's numbered fixes (dedup, coalescing,
  orphan-review clearing) were earned. Mitigation: each slice opens with
  an oracle pass over the corresponding v1 subsystem, distilled into
  spec/tests — referenced, never copied.
- **R-c — swap blast radius (replaces old R1).** Mitigation: default
  parity-swap (S-phases prove in harness first); D5'' decided in this
  section before S0.
- **R-d — D8 side effects — OPEN as staged.** Env-scoped override during
  build; control session in S4; global flip last. (Unchanged.)
- **R-e — snapshot growth — measured, designed-in.** 4,240 snapshots /
  ~96 MB / ~23 KB avg / 710 KB max (80 files). Codec caps in
  `durability/snapshots.ts` from day one; end-of-file scan; count-pruning a
  deferred backstop. (Unchanged.)
- **R-f — reviewer transport.** Abort bridge + subagent availability vary
  by environment. Mitigation: `ReviewRunner` interface isolates it; S2
  proves launch/cancel live first; no-reviewer fallback path specified
  and tested.
- **Q1 — archive location: LOCKED** as `.pi/quest/archive/pi-quest-v1/`
  (v1 archived as an artifact among quest archives).
- **Q2 — in-flight quest data: LOCKED** as cold start with deletion (no
  retrocompat; `current/` + `future/` working state deleted at S0,
  `archive/*.zip` retained read-only).
- **Q3 — D5'' swap strategy: LOCKED** as immediate swap (skeleton takes
  the slot at S0; resilience rule holds throughout).

## 13. References

- Product (normative): `HIGH_LEVEL.md` — Part 1 frozen product definition;
  Part 2 describes runtime-artifact storage only (never source locations;
  the file-by-file blueprint lives in §5 of this plan).
- Behavior: `.pi/extensions/pi-quest/docs/PRODUCT_SPEC_v2.md` Part II
  (slices 1–3 land here; slice 1 lifecycle+gates drafted, pending rewrite
  against §B0).
- Design record: Part I of the product spec (architecture tiers).
- Environment evidence: `~/.pi/agent/sessions/<project-dir>/*.jsonl`
  (transcript + snapshots),
  `~/.pi/agent/npm/node_modules/pi-vcc` (summarizer + `settings.ts:7`
  `PI_VCC_CONFIG_PATH`),
  `~/.pi/agent/pi-vcc-config.json` (D8 lives here)
- Session-file evidence (2026-09-04): snapshots are
  `type: "custom"/"custom_message"`, `customType: "quest_journal"`; 33
  appended `compaction` entries across 80 files prove append-only history
  under both compactors
- v1 oracle (post-S0): archive dir + tag `pi-quest-v1-baseline`

---

## Appendix A — Superseded rebuild plan v1 (record, not active)

The first 2026-09-04 rebuild plan (vacate slot, from-scratch v2, Ph0
golden contracts, bottom-up build, parity gate) was superseded the same
day: vcc dormant by default and its summary cannot carry quest state
(wrong §1 premise); the 54-file test suite plus numbered edge-case fixes
are a safety net clean-room discards (contract drift); snapshot stream +
per-turn injection already exist (deletion-plus-swap, not
reimplementation). Entirely subsumed by the present plan's evidence base.

## Appendix B — Decision trail (record)

1. **Wholesale rethink** over incremental slimming (cleanliness).
2. **Move-out + from-scratch in place** over separate repo (slot reuse keeps
   discovery, zip path, skill install working).
3. **Swap early** → rescinded in favor of decided-in-§12 (pending Q3).
4. **In-place modification interlude** (same day): kept the test net and
   avoided drift, but preserved v1's smeared-logic architecture — the very
   thing the user rejected ("CLEAR AND SIMPLE… re-think from scratch").
   Carried forward into this plan: discard set (§3), B1–B14 decisions,
   risk evidence (R-d staging, R-e measurements, P-comp proof), spec
   slices 1–3, doc checklist.
5. **Layered pure-core architecture** (A1 reducer, A2 interface, A3
   messaging templates) over v1-mirrored tree.
6. **HIGH_LEVEL.md promoted to normative authority**; spec Part II
   elaborates it; B0 skeleton + B5–B9 + B11–B14 locked from it.
