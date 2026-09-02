# pi-quest + site issues — index (from run 1788280759, extended 2026-09-01 with REMAINING_WORK.md + manual pi testing 2026, 2026-09-02 with consumer-complexity analysis quest #1788359911)

These files describe issues **in the `pi-quest` extension** (`.pi/extensions/pi-quest`) and the host site (`mods/*/ux`). All were observed during run `1788280759` plus `REMAINING_WORK.md` §2 (18-29), §2.6-2.7 & §3-§5+§7 (30-35), manual pi drafting gaps 36-39, `1788299416` gaps 40-42, and phases 43-45. `REMAINING_WORK.md` can be deleted — this directory is now the single source.

**Deterministic next:** each file has YAML frontmatter `state` (`ready`/`blocked`/`deferred`/`in_progress`/`done`), `requires`/`blocked_by`/`validates`, `severity`, `parent`. Run `python docs/issues/next.py` for the DAG-aware `NEXT` — `requires ⊆ done` and parents require children (`parent` blocked until all children `done`; children inherit ancestor requires — `44 requires [43]` blocks all Parent 44 leaves until drafting done; strict phase gate) sorted `severity High→Medium→Low` then `id`; single-parent tree. Mark `state: done` when verified in `pi`, then re-run. `docs/FIX_ORDER.md` (steps 1→7) is the human test script.

| # | File | Area | State | Requires | Parent | Severity |
|---|------|------|-------|----------|--------|----------|
| 59 | `59-pi-adapter-legacy-delegation-payload.md` | `critical_agent/pi_adapter.ts:22…` | ready | - | 43 | High |
| 60 | `60-consumer-complexity-dead-code-and-statics.md` | `htdocs/bud-hydrate.js, libbud…` | ready | - | - | Medium |
| 01 | `01-draft-t0-shallow-requirements-empty.md` | `markdown/template/header.ts:36…` | ready | - | 43 | Medium |
| 02 | `02-append-dedup-silent-loss.md` | `paths.ts:291-319, hooks/index.…` | ready | - | 43 | Low |
| 03 | `03-draft-last-saved-hash-never-set.md` | `state.ts:111-114,228-232, hook…` | ready | - | 43 | Medium |
| 04 | `04-promotion-paths-delete-without-archive.md` | `tools/update/executor.ts, life…` | blocked | - | 44 | High |
| 05 | `05-execution-log-duplication-and-ram-only-state.md` | `logging/paths.ts:19-55, loggin…` | blocked | - | 44 | Medium |
| 06 | `06-restart-drops-draft-lineage.md` | `persistence.ts:15-46, state.ts…` | ready | - | 43 | High |
| 07 | `07-diagnostic-hierarchy-misses-future-archive.md` | `diagnostic/hierarchy/resolve.t…` | blocked | 04 | 45 | Medium |
| 08 | `08-verbatim-draft-prompts-not-durable.md` | `hooks/index.ts:284-302,391, pa…` | ready | - | 43 | High |
| 09 | `09-synthetic-filtered-fire-and-forget-race.md` | `messaging.ts:286-327 shouldCap…` | ready | - | 43 | Medium |
| 10 | `10-investigation-bash-misclassified-as-tool-failure.md` | `tool_gating.ts, utils/shell_pa…` | ready | - | 43 | High |
| 11 | `11-direct-quest-md-write-not-prevented.md` | `gates.ts: PROVISIONAL_RESEARCH…` | ready | - | 43 | High |
| 12 | `12-save-verification-strict-files-modified.md` | `validation/consistency/audit.t…` | blocked | - | 44 | Medium |
| 13 | `13-plan-review-suppression-not-audited.md` | `tools/update/executor.ts:223-2…` | blocked | - | 44 | Low |
| 14 | `14-attempt-incremented-under-enriched.md` | `critical_agent/policy/launch_g…` | blocked | - | 44 | Low |
| 15 | `15-step-summary-missing-on-boundary.md` | `tools/update/executor.ts:315-3…` | blocked | - | 44 | Low |
| 16 | `16-concurrent-sessions-not-coalesced.md` | `critical_agent/policy/launch_g…` | blocked | - | 44 | High |
| 17 | `17-dag-allowlisted-cycles-and-project-root-pollution.md` | `constants.ts, diagnostic/hiera…` | blocked | - | 45 | Low |
| 18 | `18-snapshot-fallback-doc-stale.md` | `critical_agent/snapshot.ts:37,…` | blocked | - | 45 | Low |
| 19 | `19-draft-conversational-ignored-unconditional.md` | `hooks/index.ts:280-337, 336, c…` | ready | - | 43 | Low |
| 20 | `20-manifest-filteredCount-null.md` | `diagnostic/hierarchy/resolve.t…` | blocked | 25 | 45 | Medium |
| 21 | `21-silent-return-null-without-dedup-hit.md` | `critical_agent/policy.ts:405,4…` | blocked | - | 44 | Medium |
| 22 | `22-draft-promoted-zero-emitter.md` | `logging/types.ts:148, logging/…` | blocked | - | 44 | Low |
| 23 | `23-mutex-wait-dynamic-import-swallow.md` | `utils/mutex.ts:20,30, logging/…` | blocked | - | 44 | Low |
| 24 | `24-fifth-promotion-path-bare-rename.md` | `commands/quest.ts:49-65, 55, l…` | blocked | 04 | 44 | High |
| 25 | `25-futureCount-shadow-and-isFile-bug.md` | `diagnostic/hierarchy/resolve.t…` | blocked | 07 | 45 | Medium |
| 26 | `26-pi-restart-orphan-without-activeDraft.md` | `reconstruction.ts:24-148, 28-3…` | blocked | 06 | 43 | High |
| 27 | `27-isStoredState-blind-cast.md` | `reconstruction.ts:18-19, types…` | blocked | 26 | 44 | Medium |
| 28 | `28-as-any-213-regression.md` | `grep -rn 'as any' src --includ…` | blocked | 27 | 44 | Low |
| 29 | `29-bare-catch-swallow.md` | `grep -rn 'catch {}' src | wc -…` | blocked | 23 | 44 | Low |
| 30 | `30-epics-a-b-c-d.md` | `config.ts, gates.ts, policy.ts` | deferred | 33 | 45 | Low |
| 31 | `31-obligations-lifecycle.md` | `types.ts, transitions.ts, stat…` | deferred | 33 | 45 | Low |
| 32 | `32-three-dir-invariant.md` | `constants.ts, diagnostic/packa…` | blocked | 05,06,07,17 | 45 | Medium |
| 33 | `33-verification-matrix.md` | `diagnostic/packaging.ts, diagn…` | blocked | 18,19,20,21,22,23,24,25,26,27,28,29,32 | 45 | Medium |
| 34 | `34-rollout-order.md` | `§2.1-2.14, §4, scripts/check-p…` | deferred | 33 | 45 | Low |
| 35 | `35-archive-map.md` | `.pi/extensions/pi-quest/docs/,…` | blocked | - | 45 | Low |
| 36 | `36-quest-update-state-bypasses-draft-approval.md` | `tools/update/executor.ts:syncQ…` | done | - | 43 | High |
| 37 | `37-cross-session-mutex-ram-only.md` | `utils/mutex.ts:questLockChains…` | done | 36 | 44 | High |
| 38 | `38-draft-followup-does-not-invalidate-pending-review.md` | `hooks/index.ts:before_agent_st…` | done | 36,37 | 43 | Medium |
| 39 | `39-human-early-approve-must-not-block-re-draft.md` | `classification.ts:acceptRootCo…` | ready | 36,37,38 | 43 | Medium |
| 40 | `40-skill-quest-journal-not-invoked.md` | `hooks/index.ts:546 registerQue…` | ready | - | 43 | High |
| 41 | `41-bash-quest-md-bypass-misrouted-as-tool-failure.md` | `tool_gating.ts:222, validation…` | blocked | 10 | 43 | High |
| 42 | `42-research-complete-never-flips-despite-evidence.md` | `research.ts recordObservedInve…` | ready | - | 43 | Medium |
| 43 | `43-drafting-phase.md` | `phase/drafting` | blocked | - | - | High |
| 44 | `44-implementing-phase.md` | `phase/implementing` | blocked | 43 | - | High |
| 45 | `45-archive-phase.md` | `phase/archive` | blocked | 44 | - | High |

Deterministic next: `python docs/issues/next.py` → DAG-aware `requires⊆done` + parents require children + strict phase gate sorted `severity→id`. `python docs/issues/next.py --ready` lists all `ready` grouped by `Parent`. `44 requires [43]`, `45 requires [44]`. Manual fix order (human guide): `docs/FIX_ORDER.md` — 1: #36 draft gate, 2: #37 filesystem mutex, 3: #38 draft invalidation, 4: #39 per-boundary, 5: #40 skill, 6: #41/#42, 7: #20/#25/#26/#27 greppability.
