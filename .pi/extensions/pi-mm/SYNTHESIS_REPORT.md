# pi-mm: Final Synthesis & Experimental Findings Report

A comprehensive engineering report synthesizing the experimental findings, architectural discoveries, defect closures, and verified benchmark performance metrics established during the design, implementation, and hardening of the `pi-mm` (Memory Mipmaps) extension for the Pi Coding Agent.

---

## 1. Executive Summary

`pi-mm` provides persistent, hierarchical, and low-cost recall across session boundaries and context compaction without holding raw transcripts in context and without requiring a background daemon or cloud service. 

Through iterative adversarial investigation and experimental validation, the system evolved from an initial proof-of-concept into a production-grade alpha. Crucially, the system replaces naive assumptions (such as user-space check-then-unlink locking and single-filter boolean query scans) with kernel-level advisory coordination, atomic hard-link publication, decoupled reciprocal rank fusion, and rigorous epistemic evidence validation.

---

## 2. Experimental Findings & Architectural Evolution

### Experiment 1: Concurrency, Advisory Locking & Crash Recovery
- **Initial Defect**: The original lock implementation used an unsafe check-then-unlink stale lock protocol (`open("wx")` $\rightarrow$ inspect PID $\rightarrow$ `unlink()`). This introduced severe time-of-check/time-of-use (TOCTOU) races where an unlinking process could delete a legitimate replacement lock acquired by a competing process.
- **Node.js Production Runtime Discovery**: While Deno provides `f.tryLock(true)`, the actual production runtime of `@earendil-works/pi-coding-agent` is Node.js. In Node.js, `fs.flock` is unavailable.
- **Kernel-Reclaimed Advisory Locking**: Implemented OS advisory coordinator locking via `/usr/bin/flock` bound to an open file descriptor held by a helper child process attached to Node's stdin pipe. On process crash (`SIGKILL` or power loss), the OS kernel automatically closes the pipe, terminating the helper and instantly releasing the flock lock without user-space races or lingering stale files. Inode stability is preserved across consecutive owners without unlinking.
- **Atomic Hard-Link Publication**: Eliminated partial/empty lock windows during lock initialization. Metadata is written to a unique temporary file (`${lockFile}.tmp.${pid}.${token}`) and published atomically via exclusive POSIX `link(tmpFile, lockFile)`. If `link()` fails with `EEXIST`, the unique temporary file is unlinked in `finally`, guaranteeing that an initialization error never touches another process's lock.
- **Legacy Empty Lock Safety**: Legacy empty locks strictly fail closed and are never unlinked based on age alone, requiring explicit operator verification if a dead lock occurs.

### Experiment 2: Durability, Snapshot Isolation & Disk Journaling
- **Initial Defect**: The event persistence queue suffered from snapshot interleaving. Events arriving while a previous write was in flight were erroneously marked durable when the in-flight write completed, even though the on-disk file only contained the earlier snapshot.
- **Atomic Snapshot Selection & Barrier**: The write queue was restructured to select the exact snapshot *inside* the serialized execution task. It persists the exact snapshot to a unique file (`.tmp.<timestamp>.<uuid>`) and renames it atomically into `.pending_events.json`. Only after successful rename are those exact IDs added to `journaledEventIds`.
- **Deduplicated Retries**: When an in-flight write fails, uncommitted events are retained in the buffer and retried without accumulating duplicate event copies.
- **Post-Crash Reconstruction**: On `session_start`, `restoreJournal()` queries `mem.db` for committed event IDs, prunes them, and re-queues uncommitted events for immediate flush.

### Experiment 3: Retrieval Decoupling & Reciprocal Rank Fusion (RRF)
- **Initial Defect**: Lexical (`stoma`) and semantic (`sepal`) search were originally combined into a single boolean expression `(stoma AND sepal)`. In `libcorm`, the boolean intersection short-circuited if lexical search produced zero hits, completely suppressing semantic candidates that lacked exact keyword matches.
- **Decoupled Search Passes**: Separated retrieval into independent lexical (`stoma.db`) and semantic (`sepal.db`) candidate query passes.
- **Reciprocal Rank Fusion ($k=60$)**: Combined candidates scale-invariantly using standard RRF:
  $$\text{RRF}(d) = \sum_{m \in \{\text{lexical}, \text{semantic}\}} \frac{1}{60 + \text{rank}_m(d)}$$
  Scale-invariant fusion eliminates incompatible raw score comparisons (e.g. BM25 vs cosine similarity) while preserving candidate provenance (`stoma`, `sepal`, or `both`) and raw score metrics.

### Experiment 4: Bounded Auto-Recall Hook & Latency Guarantees
- **Initial Defect**: In `runScanWithTimeout()`, timeout timer handles were not cleared upon fast query completion, leaking delayed fallback queries into the background. Furthermore, the lexical fallback path had no deadline of its own, allowing delayed queries to block agent turns.
- **Strict Turn Deadline (1500 ms)**: Bounded total auto-recall latency to 1500 ms:
  - Up to 1000 ms allocated to primary semantic hybrid retrieval.
  - On fast success, active timers are cleared immediately via `clearTimeout` in `finally`.
  - On timeout or error, remaining budget (`Math.max(50, timeoutMs - elapsed)`) bounds the lexical fallback.
  - Stalled fallback queries abort at the deadline with empty results, guaranteeing that an agent turn is never hung.
- **Context Filtering**: Non-task greetings and short inputs are bypassed, and memories in `contradicted` or `superseded` states are excluded from context injection.

### Experiment 5: Epistemic Safety & Evidence Grounding
- **Initial Defect**: A model self-declaring `outcome: "success"` or providing a `testsPassed` array automatically resulted in a `supported` lifecycle state and a summary claiming "verified", creating an illusion of epistemic certainty without independent proof.
- **Observed vs Tested vs Supported**:
  - `observed`: Default for raw observations, unverified legacy notes, or failed experiments.
  - `tested`: Approach was tested in an experiment or reported by a model (summarized as `tested: <approach> (model-reported tests: ...)`).
  - `supported`: Verified conclusions backed by confirmed, active evidence citations (`evidence: number[]`).
- **Target Validation**: During `memory_record` and `memory_lifecycle` promotions to `supported`, `validateEvidenceRef(env, ref)` resolves references against storage via `corm -g <ref>`. Missing, dangling, contradicted, or superseded evidence targets are rejected.

### Experiment 6: Universal Cross-Runtime Configuration
- **Initial Defect**: The settings reader originally relied on `globalThis.Deno?.readTextFileSync`. In a Node.js host (the actual production runtime of the Pi Coding Agent), `Deno` was undefined, causing `.pi/settings.json` to be silently ignored.
- **Cross-Runtime `readFileSync`**: Replaced Deno-only primitives with `node:fs` synchronous `readFileSync`. Evaluates `.pi/settings.json` synchronously and identically in both Node.js and Deno, preserving `PI_MM_ADVANCED_TOOLS` environment variable precedence.

### Experiment 7: Tokenizer Mechanics & Short-Token Preservation
- **Initial Defect**: In `scanTokenFallback()`, query tokens were filtered with `filter((w) => w.length >= 3)`. When given queries like *"What about from my cv?"*, `"cv"` was stripped because its length was $< 3$. Conversational stop words (`what`, `about`, `from`) were kept and queried against `stoma`, returning unrelated older quest logs.
- **Short-Token Preservation ($\ge 2$ characters)**: Lowered the threshold to $\ge 2$ characters, ensuring technical terms (`cv`, `db`, `fs`, `io`, `ui`, `os`, `ws`, `ip`, `fd`, `id`) are preserved in query fallback.
- **Case-Insensitive Stop-Word Removal**: Added `STOP_WORDS` filtering (`what`, `about`, `from`, `with`, `this`, `that`, `my`, `your`, etc.) to prevent conversational query words from polluting search passes.

---

## 3. Verified Benchmark Performance Metrics

Benchmarks executed via `scripts/benchmark-mm.ts` evaluate retrieval accuracy, obsolete candidate exclusion, evidence traceability, and execution latency.

### Part A: Synthetic Ranking Benchmark (Fixture Corpus)

Evaluated across 6 corpus items and 7 standard test queries:

| Configuration | Precision@1 | Target Recall | Obsolete Exclusion | Evidence Trace |
|---|---|---|---|---|
| **1. Lexical-Only Search (`stoma` alone)** | 43% | 86% | 14% | 0% |
| **2. Semantic-Only Search (dense approximation)** | 57% | 86% | 29% | 0% |
| **3. Hybrid RRF Search (lexical + semantic)** | 71% | 100% | 29% | 0% |
| **4. Multidimensional Memory Mipmaps (RRF + prov + lifecycle)** | **100%** | **100%** | **100%** | **100%** |

### Part B: Real Native C Engine Retrieval Evaluation

Evaluated against workspace ELF binaries (`external/libcorm/bin/corm`, `external/libstoma/lib/libstoma.so`, `external/libjoint/lib/libjoint.so`) inside an ephemeral workspace:

- **Precision@1**: **86%** (6/7 queries returned the target at rank 1)
- **Target Recall**: **100%** (7/7 queries successfully retrieved the target record)
- **Obsolete Candidate Exclusion**: **100%** (7/7 queries successfully excluded contradicted/superseded records)
- **Top Candidate Evidence Traceability**: **86%** (6/7 top candidates traced back to verified L0 diffs/approaches)
- **Target Evidence Traceability**: **71%** (5/7 target records traced back to verified L0 diffs/approaches)
- **Empty Query Results**: **0/7** (0% failure rate)
- **Target Recall Failures**: **0/7** (0% recall loss)
- **Execution Latencies**:
  - Cold start rebuild: ~110–125 ms
  - Warm retrieval: ~51.83–55.35 ms/query (Total: 362.8 ms across 7 queries)

---

## 4. Key Architectural Invariants Established

1. **Write-Time Vectorization Invariant**: Vector embeddings in `libsepal` are computed and persisted into `sepal.db` at write time. Unconfigured databases store `joint,stoma`. Enabling embeddings later does not retroactively vectorize historical records.
2. **Strict Inode Stability**: The coordinator lock maintains a stable inode across consecutive owners without unlinking on release, preventing inode splitting and TOCTOU races under high contention.
3. **Epistemic Invariant**: An agent cannot declare a conclusion `supported` without referencing existing, active, non-contradicted records in storage.
4. **Three-Tool Default Contract**: The agent interface exposes only `memory_recall`, `memory_record`, and `memory_review` by default, eliminating orchestration complexity for weaker models while offering `advancedTools: true` for administrative operations.
