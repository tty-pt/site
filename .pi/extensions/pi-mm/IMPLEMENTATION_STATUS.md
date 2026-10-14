# pi-mm: Implementation Status and Capability Inventory

Maintained per Phased Implementation Mandate, Final Behavioural Integrity Gate, Final Data-Integrity Gate, and Final Journal & Locking Defect Closure.

## Summary Table

| Capability | Description | Status | Verification Evidence |
|---|---|---|---|
| **1. Episode Batching & Resilient Journal Durability** | In-flight batch isolation, batch-scoped diff/approach extraction, serialized atomic temp-file replacement, error recovery in write queue, lifecycle flush guard. | `VERIFIED` | `src/hooks/events.ts`: `beginFlush()` isolates the in-flight event batch; `commitFlush()` marks only successfully stored event IDs as flushed; `extractBatchApproaches()` and `extractBatchDiffs()` extract exclusively from the in-flight batch. `saveJournal()` chains with `.catch()` to recover after transient write errors, exposes failures in `activeSessionBuffer.lastError`, and guarantees atomic temporary-file cleanup. `onSessionStart()` preserves `isFlushing` and `inFlightBatch` during in-flight database stores. `flushSessionBuffer()` serializes overlapping hook calls via `activeFlushPromise`. Tested in `tests/episode.test.ts` (18/18 passed). |
| **2. Strictly Fail-Closed Locking & Race-Free Stale Recovery** | Strict ESRCH dead-PID validation, fail-closed malformed lock handling, atomic isolation without replacement-lock overwrite races, guaranteed queue release on setup failure. | `VERIFIED` | `src/lock.ts`: `isPidAlive()` returns false only on confirmed `ESRCH`, failing closed on ambiguous errors or `EPERM`. Malformed lock files are treated as unknown ownership (fail-closed) and are never pruned without confirmed ESRCH. `tryRecoverStaleLock()` isolates candidate locks via atomic `rename(lockFile, recoveryPath)` and discards/unlinks the isolated path without ever renaming back to `lockFile` (eliminating replacement-lock overwrite races). `withLock()` executes `mkdir` and lock acquisition inside `try { ... } finally { releaseInProcess(); }`, ensuring the in-process queue is always released on filesystem setup failures. Tested with competing OS child processes racing over stale locks in `tests/concurrency.test.ts` (13/13 passed). |
| **3. Truthful Evidence Consolidation & Hypothesized Fallback** | Genuine evidence required for invariants; hypothesized lifecycle when synthesis model is unconfigured; preservation of L1s; honest summary wording. | `VERIFIED` | `src/tools/consolidate.ts`: does not promote investigations to supported invariants based on shared tokens alone; filters out stop words; rejects conflicting findings (`always` vs `never`); names summaries `Hypothesized rule for ...` matching their `hypothesized` lifecycle. Automatically reports `l3Status: "unavailable (synthesis model not configured)"`. Tested in `tests/consolidate.test.ts` (13/13 passed). |
| **4. Honest Benchmark Metrics & Disaggregated Evidence Verification** | Obsolete exclusion requiring target recall; leaf verification requiring actual diffs or worked approaches; disaggregated target vs top-candidate traversal. | `VERIFIED` | `scripts/benchmark-mm.ts`: `verifyL0EvidenceLeaf()` requires actual recorded diffs (`episode.diffs`) or verified worked approaches (`episode.approaches`); removes summary text length fallback. Obsolete candidate exclusion is credited only when the target is recalled and obsolete records are excluded (`hasTarget && !hasObsolete`). Reports `Target Evidence Traceability` (71%, 5/7) disaggregated from `Top Candidate Evidence Traceability` (86%, 6/7). Reports empty results (0/7) and target recall failures (0/7) separately. Verified via regression fixtures in Part C. |
| **5. Spatial Candidate Intersection** | Auxiliary `mem-islet.db` spatial query intersected with primary text/time query. | `VERIFIED` | `src/tools/store.ts` (`storeSpatialGrid`) and `src/tools/scan.ts`. Intersects candidate refs from `libislet.so` (`--dim`, `--s`, `--l`) with primary `corm` scan results. Verified against real ELF libraries in `tests/spatial_integration.test.ts`. |
| **6. Live Remote Sepal Embeddings** | Live external network HTTP embedding server for `libsepal`. | `UNVERIFIED` | Tested with local offline soft degradation (`details.embed: "unconfigured"`). Live remote vector generation is unverified in offline test environments. |

---

## Benchmark Observed Results (`scripts/benchmark-mm.ts`)

### Part A: Synthetic Ranking Benchmark (Fixture Corpus)

```text
Configuration                            | P@1    | Recall | Obsolete Excl | Evidence Trace
-----------------------------------------+--------+--------+---------------+---------------
1. Raw Episode Search (stoma alone)      | 43%    | 86%    | 14%           | 0%           
2. Flat Episode Summaries                | 43%    | 86%    | 14%           | 0%           
3. Summaries + Semantic Approx (offline) | 71%    | 100%   | 29%           | 0%           
4. Multidimensional Memory Mipmaps       | 100%   | 100%   | 100%          | 100%         
```

### Part B: Real Native C Engine Retrieval Evaluation

Executed against workspace `external/libcorm/bin/corm`, `external/libstoma/lib/libstoma.so`, `external/libjoint/lib/libjoint.so`:

```text
Real C Engine Results (libcorm + libstoma + libjoint):
- Precision@1: 86% (6/7)
- Target Recall: 100% (7/7)
- Obsolete Candidate Exclusion (with Target): 100% (7/7)
- Target Evidence Traceability: 71% (5/7)
- Top Candidate Evidence Traceability: 86% (6/7)
- Empty Results: 0/7
- Target Recall Failures: 0/7
- Backend Workspace: Ephemeral temp directory
```

### Part C: Benchmark Regression Fixture Validations

```text
ok - summary-only L0 record rejected as evidence
ok - diff-bearing L0 leaf verified
ok - worked-approach L0 leaf verified
ok - missing target recall does not award obsolete exclusion credit
```

---

## External Prerequisites & Native Components

The extension runs in Node/Deno with TypeScript and interfaces with native C shared libraries via the `qmap` / `corm` CLI contract:
1. `libcorm` (`qmap`): System binary or workspace at `external/libcorm/bin/corm`.
2. `libjoint`: `external/libjoint/lib/libjoint.so`. Requires date/JSON patch in `patches/libjoint-date-json.patch` to support `<DATE>:<JSON>` payloads.
3. `libstoma`: `external/libstoma/lib/libstoma.so`.
4. `libislet`: `external/libislet/lib/libislet.so` (for 2D spatial grid indexing).
5. `libsepal`: Optional embedding axis. Unconfigured degrades soft (`details.embed: "unconfigured"`).

---

## Quality Gate Checklist

- **Full Unit & Integration Suite**: 126 passed, 0 failed across 14 test suites (`deno test`).
- **Static Analysis**: 37 files checked with 0 lint errors (`deno lint`).
- **Complexity Budgets**: All functions $\le 80$ LOC, all files $\le 350$ LOC (`check-complexity.ts`).
- **Real C Engine Integration**: `scripts/integration-mm.sh` and `tests/spatial_integration.test.ts` pass all green against real ELF shared libraries.
- **Distribution Bundle**: Packaged into clean `pi-mm-bundle.zip` (55 files, ~92 KB compressed, 282.8 KB uncompressed, 0 relative parent path entries).
