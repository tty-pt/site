# pi-quest issues — index (from run 1788280759, extended 2026-09-01 with REMAINING_WORK.md + manual pi testing 2026)

These 39 files describe issues **in the `pi-quest` extension** (`.pi/extensions/pi-quest`), not in the host site (`mods/*/ux`). All were observed during run `1788280759` plus `REMAINING_WORK.md` §2 (18-29), §2.6-2.7 & §3-§5+§7 (30-35), and manual pi drafting gaps 36-39. `REMAINING_WORK.md` can be deleted — this directory is now the single source.

**Deterministic next:** each file has YAML frontmatter `state` (`ready`/`blocked`/`deferred`/`in_progress`/`done`), `requires`/`blocked_by`/`validates`, `severity`. Run `python docs/issues/next.py` for the DAG-aware `NEXT` (FIX_ORDER priority: 36 → 37 → 38 → 39). Mark `state: done` when verified in `pi`, then re-run to get the next `ready`. `docs/FIX_ORDER.md` (steps 1→5) is the human test script for 36-39.

| # | File | Area | State | Requires | Severity |
|---|------|------|-------|----------|----------|
| 01 | `01-draft-t0-shallow-requirements-empty.md` | `markdown/template/header.ts` `paths.ts` | ready | - | Medium |
| 02 | `02-append-dedup-silent-loss.md` | `paths.ts` | ready | - | Low |
| 03 | `03-draft-last-saved-hash-never-set.md` | `state.ts` | ready | - | Medium |
| 04 | `04-promotion-paths-delete-without-archive.md` | `lifecycle.ts` `commands/promote.ts` `paths.ts` | ready | - | High |
| 05 | `05-execution-log-duplication-and-ram-only-state.md` | `logging/paths.ts` `lifecycle/archive/removal.ts` | ready | - | Medium |
| 06 | `06-restart-drops-draft-lineage.md` | `reconstruction.ts` `persistence.ts` | ready | - | High |
| 07 | `07-diagnostic-hierarchy-misses-future-archive.md` | `hierarchy/resolve.ts` `packaging.ts` | blocked | 04 | Medium |
| 08 | `08-verbatim-draft-prompts-not-durable.md` | `hooks/index.ts` | ready | - | High |
| 09 | `09-synthetic-filtered-fire-and-forget-race.md` | `messaging.ts` | ready | - | Medium |
| 10 | `10-investigation-bash-misclassified-as-tool-failure.md` | `tool_gating.ts` `research.ts` | ready | - | High |
| 11 | `11-direct-quest-md-write-not-prevented.md` | `gates.ts` | ready | - | High |
| 12 | `12-save-verification-strict-files-modified.md` | `validation/consistency/audit.ts` | ready | - | Medium |
| 13 | `13-plan-review-suppression-not-audited.md` | `tools/update/executor.ts` | ready | - | Low |
| 14 | `14-attempt-incremented-under-enriched.md` | `policy/launch_guard.ts` | ready | - | Low |
| 15 | `15-step-summary-missing-on-boundary.md` | `tools/update/executor.ts` | ready | - | Low |
| 16 | `16-concurrent-sessions-not-coalesced.md` | `pending_coalesce.ts` `tracker.ts` | ready | - | High |
| 17 | `17-dag-allowlisted-cycles-and-project-root-pollution.md` | `project.ts` `check-pi-quest-dag.ts` | ready | - | Low |
| 18 | `18-snapshot-fallback-doc-stale.md` | `snapshot.ts` | ready | - | Low |
| 19 | `19-draft-conversational-ignored-unconditional.md` | `hooks/index.ts` | ready | - | Low |
| 20 | `20-manifest-filteredCount-null.md` | `hierarchy/resolve.ts` | blocked | 25 | Medium |
| 21 | `21-silent-return-null-without-dedup-hit.md` | `policy.ts` `pending_coalesce.ts` | ready | - | Medium |
| 22 | `22-draft-promoted-zero-emitter.md` | `logging/types.ts` | ready | - | Low |
| 23 | `23-mutex-wait-dynamic-import-swallow.md` | `utils/mutex.ts` | ready | - | Low |
| 24 | `24-fifth-promotion-path-bare-rename.md` | `commands/quest.ts` (extends #04) | blocked | 04 | High |
| 25 | `25-futureCount-shadow-and-isFile-bug.md` | `resolve.ts` | blocked | 07 | Medium |
| 26 | `26-pi-restart-orphan-without-activeDraft.md` | `reconstruction.ts` (extends #06) | blocked | 06 | High |
| 27 | `27-isStoredState-blind-cast.md` | `reconstruction.ts` `types.ts` | blocked | 06,26 | Medium |
| 28 | `28-as-any-213-regression.md` | `grep as any 213→0` | blocked | 27 | Low |
| 29 | `29-bare-catch-swallow.md` | `121 bare catch{}` → `logging/safe.ts` | blocked | 23 | Low |
| 30 | `30-epics-a-b-c-d.md` | `config.ts`/`gates.ts`/`policy.ts` | deferred | 33 | Low |
| 31 | `31-obligations-lifecycle.md` | `types.ts` | deferred | 33 | Low |
| 32 | `32-three-dir-invariant.md` | `constants.ts` | blocked | 05,06,07,17 | Medium |
| 33 | `33-verification-matrix.md` | `§11.4+§11.6.4+§11.7.4` `grep` matrix | blocked | 18,19,20,21,22,23,24,25,26,27,28,29,32 | Medium |
| 34 | `34-rollout-order.md` | `§5` ordered stages `55→57→58→62` | deferred | 33 | Low |
| 35 | `35-archive-map.md` | `§7` 8 to-be-removed docs → `docs/archive/` | ready | - | Low |
| 36 | `36-quest-update-state-bypasses-draft-approval.md` | `executor.ts:syncQuestIdentity` | done | - | High |
| 37 | `37-cross-session-mutex-ram-only.md` | `utils/mutex.ts` RAM-only | done | 36 | High |
| 38 | `38-draft-followup-does-not-invalidate-pending-review.md` | `hooks/index.ts:draft` | done | 36,37 | Medium |
| 39 | `39-human-early-approve-must-not-block-re-draft.md` | `gates.ts:31` per-boundary | ready | 36,37,38 | Medium |

Deterministic next: `python docs/issues/next.py` → `36` first (FIX_ORDER step 1), then `37` after `36:done`, etc.; `python docs/issues/next.py --ready` lists all `ready` sorted `FIX_ORDER` → severity → id. After marking `36 state:done`, `37` becomes `ready` (blocked_by cleared). Manual fix order (testable in `pi`): `docs/FIX_ORDER.md` — 1: #36 draft gate, 2: #37 filesystem mutex, 3: #38 draft invalidation, 4: #39 per-boundary, 5: #20/#25/#26/#27 greppability. Detailed `rg`/`unzip`/`deno` in `#33`. Original gaps: Gaps 1-6 cover #13,#14,#03,#09,#15,#07; Gap 7 covers #06; persistence §3 covers #01,#04,#05,#08; remaining (#02,#10,#11,#12,#16,#17) tracked here. `REMAINING_WORK.md` §2.8-2.14 (18-29), §2.6-2.7 (30-31), §3 (32), §4 (33), §5 (34), §7 (35), plus manual gaps 36-39 — single source guarantee restored.
