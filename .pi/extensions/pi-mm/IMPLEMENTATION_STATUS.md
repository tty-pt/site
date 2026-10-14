# pi-mm: Implementation Status and Capability Inventory

Maintained per Phased Implementation Mandate and Final Audit Closure.

## Summary Table

| Capability | Description | Status | Verification Evidence |
|---|---|---|---|
| **1. Truthful Consolidation** | Evidence-sensitive synthesis; never promotes contradicted or superseded memories; validates levels; leaves unpromoted when offline. | `VERIFIED` | `src/tools/consolidate.ts`: rejects promoting contradicted/superseded source memories to supported conclusions (`invalid-source`); validates level transitions (rejects L3/L2 $\rightarrow$ L1 with `invalid-level-transition`); groups L1 strictly by scope; leaves L3 unpromoted without explicit model synthesis. Tested in `tests/consolidate.test.ts` (10/10 passed). |
| **2. Safe Cross-Process Locking** | Atomic cross-process synchronization; checks PID liveness before pruning; fails on active lock contention; multi-process tested. | `VERIFIED` | `src/lock.ts` (`withLock`): in-process mutex + atomic `.mm.lock` filesystem locking (`open(lockFile, "wx")`). Uses `isPidAlive(pid)` to ensure active processes are never pruned even past stale threshold. Fails with explicit timeout error on contention. Tested with genuinely separate OS child processes via `Deno.Command` in `tests/concurrency.test.ts` (7/7 passed). |
| **3. Capture Accuracy & Crash Recovery** | Captures tool arguments from `event.input`/`args`, verifies diff headers, persists disk journal for process crash recovery. | `VERIFIED` | `src/hooks/events.ts`: reads `event.input` and `event.args`; extracts diffs only when patch headers (`@@ -`, `diff --git`) exist; persists pending unflushed events to disk journal (`.pi/mm/.pending_events.json`) to survive process crashes and restarts. Tested in `tests/episode.test.ts` (9/9 passed). |
| **4. Honest Native Benchmark** | Seeds real L0 episode diffs/approaches, validates full traversal to L0 leaves, separates empty queries from obsolete filtering. | `VERIFIED` | `scripts/benchmark-mm.ts`: Part A evaluates synthetic fixture with true obsolete exclusion and graph provenance chain traversal; Part B seeds full L0 episodes in real `libcorm` + `libstoma` + `libjoint` backend, validates multi-hop traversal to L0 leaves, and requires target recall alongside obsolete candidate exclusion. |
| **5. Spatial Candidate Intersection** | Auxiliary `mem-islet.db` spatial query intersected with primary text/time query. | `VERIFIED` | `src/tools/store.ts` (`storeSpatialGrid`) and `src/tools/scan.ts`. Intersects candidate refs from `libislet.so` (`--dim`, `--s`, `--l`) with primary `corm` scan results. Verified against real ELF libraries in `tests/spatial_integration.test.ts`. |
| **6. Live Remote Sepal Embeddings** | Live external network HTTP embedding server for `libsepal`. | `UNVERIFIED` | Tested with local offline soft degradation (`details.embed: "unconfigured"`). Live remote vector generation is unverified in offline test environments. |

---

## Benchmark Observed Results (`scripts/benchmark-mm.ts`)

### Part A: Synthetic Ranking Benchmark (Fixture Corpus)

```text
Configuration                            | P@1    | Obsolete Excl | Evidence Trace
-----------------------------------------+--------+---------------+---------------
1. Raw Episode Search (stoma alone)      | 43%    | 14%           | 0%           
2. Flat Episode Summaries                | 43%    | 14%           | 0%           
3. Summaries + Semantic Approx (offline) | 71%    | 29%           | 0%           
4. Multidimensional Memory Mipmaps       | 100%   | 100%          | 100%         
```

### Part B: Real Native C Engine Retrieval Evaluation

Executed against workspace `external/libcorm/bin/corm`, `external/libstoma/lib/libstoma.so`, `external/libjoint/lib/libjoint.so`:

```text
Real C Engine Results (libcorm + libstoma + libjoint):
- Precision@1: 29% (2/7 on exact token match without full embedding server)
- Valid Target Recall & Obsolete Exclusion: 29% (2/7 - requires target recall AND exclusion of obsolete candidates)
- Provenance Traversal to L0 Leaf: 14% (1/7 - verified traversal reaching L0 episode leaf with diffs/approaches)
- Backend Workspace: Ephemeral temp directory
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

- **Unit & Integration Suite**: 108 passed, 0 failed across 14 test suites (`deno test`).
- **Static Analysis**: 37 files checked with 0 lint errors (`deno lint`).
- **Complexity Budgets**: All functions $\le 80$ LOC, all files $\le 350$ LOC (`check-complexity.ts`).
- **Real C Engine Integration**: `scripts/integration-mm.sh` and `tests/spatial_integration.test.ts` pass all green against real ELF shared libraries.
- **Distribution Bundle**: Packaged into clean `pi-mm-bundle.zip` (55 files, 251.5 KB uncompressed, 0 parent path entries).
