# pi-mm: Implementation Status and Capability Inventory

Maintained per Phased Implementation Mandate, Final Data-Integrity Gate, Final Journal & Locking Defect Closure, Final Lock Ownership and Journal Durability Closure, Recovery Coordinator and Journal Success-Semantics Closure, Final Coordinator-Race and Duplicate-Event Retry Fix, Final Advisory Lock Lifecycle and Journal Snapshot Integrity Fix, Production Runtime Lock Recovery Closure, Main Lock Initialization and Crash-Recovery Closure, Main Lock Atomic Publication and Replacement-Race Closure, Legacy Empty-Lock Safety and Atomic Publication Boundary Verification, and pi-mm MVP Finalization.

## 1. Verified by Tests

| Capability | Description | Status | Verification Evidence |
|---|---|---|---|
| **Truthful Journal Success Semantics & Exact Snapshot Selection** | Awaited persistence barrier, exact snapshot selection inside serialized write queue, unique temp file write + atomic rename, marking only successfully persisted snapshot IDs, unsuppressed promise rejections, observable failure reporting via Pi `emitError`, retry preservation across failures, uninitialized `journalDir` rejection, duplicate-event persistence retries, deduplication without duplicate copies, observable non-silent buffer overflow, and surfaced restore errors. | `VERIFIED` | `src/hooks/events.ts`: `saveJournal()` captures its event snapshot inside the serialized queue execution, writes that exact snapshot to a unique temporary file (`.tmp.<timestamp>.<uuid>`), and atomically renames it into place. Only after successful rename are the snapshot's IDs marked in `journaledEventIds`. Tested in `tests/episode.test.ts` (31/31 passed) across both `recordAsync()` and `record()` paths. |
| **Production Runtime OS Advisory Coordinator Locking & Inode Stability** | OS-enforced advisory locking in both Node.js (Pi production runtime via `/usr/bin/flock`) and Deno (`f.tryLock(true)`), kernel-managed crash reclamation, descriptor leak elimination on metadata write error, handle unlock/close without unlinking the coordinator path, stable inode across consecutive owners, fail-closed behavior on existing coordinator locks, and serialized main-lock recovery. | `VERIFIED` (Linux/POSIX Node + Deno) | `src/lock.ts`: Recovery attempts are serialized via `acquireRecoveryCoordinationLock(".mm.lock.recovery.lock")`. In Deno, uses `Deno.open` and `fsFile.tryLock(true)`. In Node.js, acquires OS advisory locking via `/usr/bin/flock`, binding ownership to a helper process attached to Node's stdin pipe. On process crash (`SIGKILL`), OS kernel closes pipe and releases flock. Inode stability preserved without unlinking on release. Verified in `tests/concurrency.test.ts` (30/30 passed). |
| **Main Lock Atomic Publication & Fail-Closed Empty Lock Safety** | Visible empty-lock elimination via unique temp file and atomic `link()`, removal of unsafe pathname cleanup, deterministic pre-publication race protection, strict single critical-section owner invariant, and fail-closed safety for legacy empty locks (never unlinked on age alone). | `VERIFIED` | `src/lock.ts`: `acquireFileLock()` writes metadata to unique temp file (`${lockFile}.tmp.${pid}.${token}`) and publishes atomically via `link()`. `link()` fails with `EEXIST` without replacing. Error cleanup removes only unique `tmpFile`. In `tryRecoverStaleLock()`, legacy empty locks (`content.trim() === ""`) strictly fail closed without unlinking. Verified in `tests/concurrency.test.ts` (30/30 passed). |
| **Independent Retrieval Paths with Reciprocal Rank Fusion (RRF)** | Decoupled candidate retrieval paths, independent execution over `stoma` (lexical BM25) and `sepal` (semantic cosine ANN), deterministic RRF ranking ($k=60$), source attribution metadata (`stoma`, `sepal`, or `both`), and soft graceful fallback on unconfigured or failing embeddings. | `VERIFIED` | `src/tools/ranking.ts` & `src/tools/scan.ts`: Lexical and semantic candidate paths run independently instead of through a single short-circuiting `AND` filter. Candidates are fused via Reciprocal Rank Fusion ($k=60$) preserving individual ranks, raw scores, and source attribution (`origin: "stoma" | "sepal" | "both"`). Semantic hits surface even when lexical returns zero hits. Tested in `tests/hybrid.test.ts` (5/5 passed) and `tests/tools.test.ts` (19/19 passed). |
| **Automatic Bounded Memory Recall with Timer Cleanup & Bounded Fallback** | Autonomous context injection before agent turns begin, prompt relevancy gating (ignores greetings, confirmations, and empty prompts), overall deadline bounded to 1500 ms with timeout timer cleanup, bounded lexical fallback, explicit hypothesis tagging (`[HYPOTHESIS]`), and strict exclusion of `contradicted` and `superseded` memories. | `VERIFIED` | `src/hooks/auto_recall.ts` & `src/hooks/events.ts`: Attaches to `before_agent_start` when `messageType === "user"`. Filters trivial inputs, runs hybrid scan with immediate timer clearance on success, falls back to bounded lexical query on delay, formats compact context with references, summaries, lifecycle, and evidence citations, and returns `{ message: { customType: "memory_context", content, display: true } }`. Tested in `tests/auto_recall.test.ts` (10/10 passed) and `tests/pi_lifecycle.test.ts` (2/2 passed). |
| **Epistemic Safety & Evidence Grounding** | Models cannot claim `supported` merely by asserting `outcome: "success"`. Model-reported tests/diffs assign `lifecycle: "tested"` and summary `tested: <approach> (model-reported tests: ...)`. `supported` strictly requires confirmed evidence references (`evidence: number[]`) pointing to active, non-contradicted records in storage. Dangling, missing, or contradicted evidence references are rejected during `record` and `lifecycle` promotion. Legacy plain-text records default conservatively to `observed`. | `VERIFIED` | `src/tools/record.ts`, `src/payload.ts`, `src/tools/lifecycle.ts`, `src/tools/review.ts`. Verified in `tests/epistemic_safety.test.ts` (11/11 passed) and `tests/payload.test.ts` (4/4 passed). |
| **Streamlined Model Workflow & Cross-Runtime Advanced Tools Gate** | High-level operations that hide low-level orchestration: `memory_recall`, `memory_record`, and `memory_review`. Synchronous `advancedTools: false` setting registers only the 3 primary tools by default in both Node.js and Deno hosts via `readFileSync`. `PI_MM_ADVANCED_TOOLS` environment variable overrides with highest precedence. | `VERIFIED` | `src/config.ts`, `src/tools/index.ts`. Tested in `tests/config.test.ts` (12/12 passed), `tests/workflow_tools.test.ts` (3/3 passed), and `tests/smoke.test.ts` (5/5 passed). |
| **Full End-to-End Workflow with Context Injection & Evidence Inspection** | 7-step user story: problem stored with evidence $\rightarrow$ differently worded prompt $\rightarrow$ automatic injection before turn starts $\rightarrow$ hypothesis tagged $\rightarrow$ obsolete memory excluded $\rightarrow$ evidence tree inspected $\rightarrow$ task outcome recorded. | `VERIFIED` | `tests/e2e_workflow.test.ts` (1/1 passed). |
| **Spatial Candidate Intersection** | Auxiliary `mem-islet.db` spatial query intersected with primary text/time query. | `VERIFIED` | `src/tools/store.ts` (`storeSpatialGrid`) and `src/tools/scan.ts`. Intersects candidate refs from `libislet.so` (`--dim`, `--s`, `--l`) with primary `corm` scan results. Verified against real ELF libraries in `tests/spatial_integration.test.ts`. |
| **Truthful Evidence Consolidation & Hypothesized Fallback** | Genuine evidence required for invariants; hypothesized lifecycle when synthesis model is unconfigured; preservation of L1s; honest summary wording. | `VERIFIED` | `src/tools/consolidate.ts`: does not promote investigations to supported invariants based on shared tokens alone; filters out stop words; rejects conflicting findings (`always` vs `never`). Tested in `tests/consolidate.test.ts` (13/13 passed). |
| **Opt-In Real Native Embedding Smoke Test with Strict Status Separation** | Opt-in integration test using real `corm` binary, `libsepal.so`, `libstoma.so`, `libjoint.so`. Strictly distinguishes `PASS` (all assertions pass, exit 0), `SKIPPED` (unconfigured/unreachable, exit 0), and `FAIL` (assertion failure, exit 1). Isolates temporary directories via `resolveMemDir` with reliable cleanup. Never falsifies pass on skips. | `VERIFIED` | `scripts/test-real-embedding.ts` and `tests/real_embedding.test.ts`. Verified in test suite (truthfully reports SKIPPED when endpoint unconfigured in offline test environment). |
| **Short-Token Technical Retrieval & Stop-Word Filtering** | Preserves 2-letter technical tokens (`w.length >= 2`) like `cv`, `db`, `fs`, `io`, `ui`, `os`, `ws`, `ip`, `fd`, `id` in multi-word fallback queries while filtering conversational English stop words (`what`, `about`, `from`, `with`, `this`, `that`, `my`, `your`, etc.), preventing query pollution on conversational inputs. | `VERIFIED` | `src/tools/scan.ts`. Tested in `tests/hybrid.test.ts` (7/7 passed). |

---

## 2. Implemented but Unverified against Real Dependencies

| Capability | Description | Status | Current Mitigation |
|---|---|---|---|
| **Live Remote Sepal Vector Generation** | Real-time HTTP vector embeddings generated by external model server (e.g. OpenAI or Ollama `/v1/embeddings` endpoint). | `UNVERIFIED` (Offline Test Environment) | Verified with local offline soft degradation (`details.embed: "unconfigured"`). Opt-in smoke test (`scripts/test-real-embedding.ts`) truthfully reports `SKIPPED` when unconfigured. |
| **Non-POSIX File Coordinator Locking Fallback** | Advisory file locking on platforms without `/usr/bin/flock` or POSIX file handles. | `UNVERIFIED` (Non-POSIX Environments) | Node.js fallback uses `open(..., "wx")` and strictly fails closed with an actionable error directing operator recovery. Automatic recovery is not claimed on unsupported platforms. |

---

## 3. Known Limitations

1. **Child Process Cancellation Boundary**: `QmapRunner` in `src/qmap.ts` executes native CLI commands through a simple runner boundary that does not provide process-level `AbortController` handles. While timed-out auto-recall queries ignore late results and strictly enforce the 1500 ms turn deadline, native child processes cannot be killed early through the runner interface alone.
2. **Automated L3 Cross-Project Synthesis**: Cross-project L3 generalization requires an external LLM synthesis provider. When unconfigured, `memory_consolidate` reports `l3Status: "unavailable (synthesis model not configured)"` and refuses speculative generalization.
3. **Accent Sensitivity**: Search is strictly accent-sensitive (`Pão ≠ pao`, no transliteration) to preserve pure lexical integrity per project conventions.

---

## Benchmark Observed Results (`scripts/benchmark-mm.ts`)

### Part A: Synthetic Ranking Benchmark (Fixture Corpus)

```text
Configuration                            | P@1    | Recall | Obsolete Excl | Evidence Trace
-----------------------------------------+--------+--------+---------------+---------------
1. Lexical-Only Search (stoma alone)     | 43%    | 86%    | 14%           | 0%           
2. Semantic-Only Search (dense approx)   | 57%    | 86%    | 29%           | 0%           
3. Hybrid RRF Search (lexical + semantic) | 71%    | 100%   | 29%           | 0%           
4. Multidimensional Memory Mipmaps (RRF+prov) | 100%   | 100%   | 100%          | 100%         
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
- Average Retrieval Latency: 55.35 ms/query (Total: 387.5 ms)
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

- **Full Unit & Integration Suite**: 196 passed, 0 failed across 21 test suites (`deno test`).
- **Static Analysis**: 50 files checked with 0 lint errors (`deno lint`).
- **Complexity Budgets**: All functions $\le 80$ LOC, all files $\le 350$ LOC (`check-complexity.ts`).
- **Real C Engine Integration**: `scripts/integration-mm.sh` and `tests/spatial_integration.test.ts` pass all green against real ELF shared libraries.
- **Native Benchmark**: precision@1 86%, target recall 100%, obsolete exclusion 100%, target trace 71%, top trace 86%, avg latency 55.35 ms.
- **Opt-In Native Real Embedding Smoke Test**: `scripts/test-real-embedding.ts` and `tests/real_embedding.test.ts` verified with truthful status reporting.
- **Distribution Bundle**: Packaged into clean `pi-mm-bundle.zip` (68 files, 136 KB compressed, 468,439 bytes (468.4 KB) uncompressed, 0 relative parent path entries).
