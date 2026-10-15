# pi-mm: Memory Mipmaps for Pi Coding Agent

Persistent, hierarchical, low-cost recall across context compaction and agent sessions without holding raw transcripts in context and without requiring a background daemon or network connection.

`pi-mm` bridges the local `libcorm` storage kernel and multi-dimensional axes (`joint` time, `stoma` lexical FTS, `sepal` semantic vector ANN, and `islet` 2D/3D spatial coordinates) directly into Pi Coding Agent. By default, it exposes a streamlined **3-tool workflow** (`memory_recall`, `memory_record`, `memory_review`) and provides **autonomous context injection** before agent turns begin.

---

## Architecture & Stack Layers

```text
┌────────────────────────────────────────────────────────────────────────┐
│                        PI CODING AGENT HARNESS                         │
│  Hooks: before_agent_start, tool_result, agent_settled, turn_end       │
│  Primary Tools: memory_recall, memory_record, memory_review            │
├────────────────────────────────────────────────────────────────────────┤
│                          TYPESCRIPT EXTENSION                          │
│  • Concurrency: Atomic link() publication & OS advisory flock/tryLock   │
│  • Durability: Awaited snapshot queue & disk journal (.pending_events)  │
│  • Ranking: Reciprocal Rank Fusion (RRF k=60) + token stop-word filter  │
│  • Epistemics: Evidence target validation (observed → tested → supported│
├────────────────────────────────────────────────────────────────────────┤
│                        CORM / QMAP CLI SURFACE                         │
│  • filespec: mem.db@joint,stoma:a:s (+ sepal when configured)          │
│  • Sub-millisecond single-pass execution via stdin/argv                │
├────────────────────────────────────────────────────────────────────────┤
│                     NATIVE C STORAGE ENGINES & AXES                    │
│  • libcorm:  Zero-copy in-memory struct DB with secondary B-trees      │
│  • libjoint: Augmented interval-tree temporal engine (who/where/when)  │
│  • libstoma: Accent-sensitive tokenizer (pão ≠ pao) & BM25 ranking     │
│  • libsepal: Semantic vector database with SIMD-accelerated ANN        │
│  • libislet: Morton space-filling curve 2D/3D bounding index           │
└────────────────────────────────────────────────────────────────────────┘
```

---

## Prerequisites & Clean Checkout Build

`pi-mm` interfaces with system or repository C binaries. From a clean workspace checkout of the `site` repository, build the native libraries using the workspace Makefile:

```bash
# Build the primary corm binary and all axis libraries
make external/libcorm
make external/libjoint
make external/libstoma
make external/libsepal   # optional: required for semantic vector search
make external/libislet   # optional: required for spatial grid coordinates
```

Default locations:
- Binary: `external/libcorm/bin/corm` (or `external/libqmap/bin/qmap`)
- Libraries: `external/libjoint/lib`, `external/libstoma/lib`, `external/libsepal/lib`, `external/libislet/lib`

---

## Primary Agent Workflow (Default Loadout)

To minimize prompt pollution and avoid confusing weaker models with a 12-tool orchestration protocol, `pi-mm` exposes only 3 primary tools by default:

### 1. `memory_recall`
One-stop hybrid search across lexical (`stoma`) and semantic (`sepal`) axes fused deterministically with Reciprocal Rank Fusion ($k=60$).
- **`query`**: Natural language problem description, compiler error, or technical symbol.
- **`project`** / **`subsystem`**: Optional scope filters to boost matching records.
- **`includeEvidence: true`**: Automatically fetches and attaches the complete underlying evidence tree (diffs, commands, prior investigations).

### 2. `memory_record`
Captures task resolutions and learnings into structured L0 episodes without low-level JSON envelope wrangling.
- **`problem`**: Description of the issue or objective.
- **`approaches`**: Array of `{ approach, outcome: "worked" | "failed" | "abandoned", reason }`.
- **`diffs`**: Commands, diffs, or file modifications.
- **`evidence`**: Optional array of supporting memory references (`evidence: number[]`).
- **`verification`**: Optional model-reported test commands (`testsPassed?: string[]`, `verifiedDiff?: boolean`).
- **`outcome`**: `"success"` | `"failure"` | `"inconclusive"`.

### 3. `memory_review`
Inspects complete evidence hierarchies or safely transitions lifecycle states.
- **`action: "inspect"`**: Returns full episode details, attempted approaches, and complete downward evidence trees.
- **`action: "update_lifecycle"`**: Safely transitions state (`tested`, `supported`, `contradicted`, `superseded`) with required justification and evidence validation.

---

## Autonomous Session Pipeline & Event Journaling

`pi-mm` operates completely autonomously in the background through Pi's native lifecycle hooks without requiring manual tool invocations to record routine session history:

```text
User Turn Starts
   │
   ▼
[before_agent_start] ──► Autonomous Context Recall (1500 ms deadline, timer cleanup)
   │
   ▼
Tool Executions (bash, edit, read...)
   │
   ▼
[tool_result]        ──► Captured into SessionEventBuffer
   │                     └─► Atomic write to disk journal (.pending_events.json)
   ▼
Agent Settles / Turn Ends
   │
   ▼
[agent_settled]      ──► Automated Batch Flush under withLock
[turn_end]               ├─► Aggregates events into structured L0 episode envelope
                         ├─► Persists episode into mem.db@joint,stoma:a:s
                         ├─► Commits event IDs & clears flushed items from disk journal
                         └─► Retains pending items if write fails
```

### 1. Context Injection (`before_agent_start`)
- Before each user turn begins, `before_agent_start` queries `mem.db` for memories matching the prompt.
- Trivial greetings and non-task inputs are filtered out.
- The turn latency is strictly bounded to a **1500 ms deadline** (~1000 ms semantic budget, remaining time for lexical fallback).
- When a fast query returns, active timeout timers are cleared immediately via `clearTimeout` in `finally`, preventing delayed background queries.
- Records in `contradicted` or `superseded` states are filtered out to prevent outdated context pollution.

### 2. Disk Journaling of Execution Traces (`tool_result`)
- Every tool completion event (`tool_result`) is intercepted by `SessionEventBuffer`.
- Tool executions (commands, file modifications, test passes, failures, diffs) are summarized into structured event records.
- Events are serialized and persisted immediately to the disk journal (`.pi/mm/.pending_events.json`) via a serialized write queue using unique temporary files (`.tmp.<timestamp>.<uuid>`) and atomic `rename()`.
- An exact snapshot boundary ensures that only successfully persisted event IDs are marked durable.

### 3. Automatic Episode Flushing (`agent_settled` & `turn_end`)
- When the agent finishes generating its response (`turn_end`) or the agent settles (`agent_settled`), `flushSessionBuffer` triggers automatically.
- Buffered events from `.pending_events.json` are aggregated into a structured **L0 Episode Record** containing problem descriptions, attempted approaches with reasons, raw observations, diffs, and overall outcome (`success`, `failure`, or `observed`).
- The episode is serialized into `<DATE>:<JSON>` format and persisted to `mem.db` under the atomic `withLock` concurrency coordinator.
- Once stored successfully, committed event IDs are cleared from `.pending_events.json`. If a write fails, unpersisted events are retained in the journal for retry.

### 4. Post-Crash Recovery (`session_start`)
- If the Pi process crashes (`SIGKILL`, power failure, or uncaught exception) mid-turn, pending events remain safely recorded on disk in `.pending_events.json`.
- Upon `session_start` in the subsequent session, `restoreJournal()` scans `mem.db` for already-committed event IDs, prunes duplicates, and restores any uncommitted events into the active buffer for immediate flushing.

---

## Configuration Guide

Configuration can be specified in `.pi/settings.json` under `"pi-mm"` or via environment variables.

### `.pi/settings.json`

```json
{
  "pi-mm": {
    "qmapBin": "/usr/bin/qmap",
    "axisLibs": "/usr/lib/joint:/usr/lib/stoma",
    "memDir": ".pi/mm",
    "scanLimit": 10,
    "advancedTools": false,
    "embedUrl": "http://localhost:11434/v1/embeddings",
    "embedModel": "nomic-embed-text",
    "embedKey": "optional-bearer-token"
  }
}
```

### Configuration Options & Precedence

| Option | Setting Key | Environment Variable | Default | Description |
|---|---|---|---|---|
| **Binary Path** | `qmapBin` | `CORM_BIN` or `QMAP_BIN` | In-site fallback | Path to `corm` or `qmap` executable. |
| **Axis Libraries** | `axisLibs` | `CORM_AXIS_PATH` or `QMAP_AXIS_PATH` | In-site fallback | Colon-separated directory list containing axis `.so` libraries. |
| **Storage Directory** | `memDir` | — | `.pi/mm` | Relative or absolute path where `mem.db` and axes reside. |
| **Scan Limit** | `scanLimit` | — | `10` | Default number of candidates returned by scan queries. |
| **Advanced Tools** | `advancedTools` | `PI_MM_ADVANCED_TOOLS` | `false` | When `false`, registers only 3 tools. When `true`, registers all 12 tools. |
| **Embedding URL** | `embedUrl` | `CORM_SEPAL_EMBED_URL` or `QMAP_SEPAL_EMBED_URL` | `undefined` | HTTP(S) endpoint for vector generation (OpenAI/Ollama format). |
| **Embedding Model** | `embedModel` | `CORM_SEPAL_EMBED_MODEL` or `QMAP_SEPAL_EMBED_MODEL` | `undefined` | Model name passed in embedding payload. |
| **Embedding Key** | `embedKey` | `CORM_SEPAL_EMBED_KEY` or `QMAP_SEPAL_EMBED_KEY` | `undefined` | Optional Authorization Bearer token. |

**Precedence**: `.pi/settings.json` $\rightarrow$ Environment Variables $\rightarrow$ System `PATH` $\rightarrow$ In-site workspace build.  
Settings resolution is synchronous and universal across both **Node.js** (Pi Coding Agent production host) and **Deno** using `node:fs` `readFileSync`.

---

## Semantic Vector Search & The Write-Time Invariant

`libsepal` provides semantic vector ANN retrieval by integrating directly into `libcorm`'s storage roster.

### The Write-Time Indexing Invariant
**Crucial Architectural Rule**: Vector embeddings are **not** computed on the fly across raw text at query time. They must be generated and persisted into `sepal.db` at **store time**:
1. When `embedUrl` and `embedModel` are configured at the time an entry is saved, `corm` opens `mem.db@joint,stoma,sepal:a:s`. Each entry's text is sent to the embedding endpoint, quantized into sketch words, and appended to `sepal.db`.
2. When embeddings are unconfigured, `corm` opens `mem.db@joint,stoma:a:s`. Zero vector data is written.
3. **Retrofitting**: Enabling `embedUrl` on an existing database that was created with `joint,stoma` does not retroactively vectorize old records. Existing records must be re-indexed or re-stored to populate `sepal.db`.
4. **Soft Degradation**: When embeddings are not configured or temporarily unreachable, `pi-mm` soft-degrades to pure lexical search (`stoma`) without throwing errors.

---

## Search & Tokenization Mechanics

`pi-mm` decouples candidate generation into independent query passes:
- **Lexical Path (`stoma`)**: Matches exact keywords, technical symbols, function names, and file paths.
- **Semantic Path (`sepal`)**: Matches conceptual intent and paraphrases when embeddings are active.
- **Reciprocal Rank Fusion (RRF $k=60$)**: Merges candidates scale-invariantly:
  $$\text{RRF}(d) = \sum_{m \in \{\text{lexical}, \text{semantic}\}} \frac{1}{60 + \text{rank}_m(d)}$$

### Short Technical Token Preservation & Stop-Word Filtering
Conversational questions asked to an agent (e.g., *"What about from my cv?"*) present unique challenges:
1. **Short Technical Tokens ($\ge 2$ characters)**: Important engineering terms like `cv`, `db`, `fs`, `io`, `ui`, `os`, `ws`, `ip`, `fd`, `id`, `c`, and `ts` are strictly preserved in query fallback.
2. **Conversational Stop-Word Removal**: Common English stop words (`what`, `about`, `from`, `with`, `this`, `that`, `have`, `been`, `my`, `your`, etc.) are filtered out during token fallback so generic filler words do not poll `stoma` and match unrelated records.
3. **Graceful Degradation**: If an entire query consists of stop words (e.g. *"what about that?"*), it falls back to unfiltered tokens without throwing errors or dispatching empty arguments.

---

## Epistemic Safety & Evidence Discipline

To prevent weaker models from hallucinating verification certainty, `pi-mm` enforces a grounded 3-tier lifecycle:

| Lifecycle State | Meaning | Requirements to Attain |
|---|---|---|
| **`observed`** | Initial observation from a tool result, error, or legacy unstructured note. | Default for new unverified notes, failed approaches, or legacy imports. |
| **`tested`** | Approach was attempted in an experiment, worked in execution, or was self-reported by a model. | Approach marked `worked: true` or model reported `testsPassed` / `verifiedDiff`. (Labeled honestly as `tested: <approach> (model-reported tests: ...)`). |
| **`supported`** | Active, verified truth backed by inspectable evidence. | Strictly requires confirmed, active evidence references (`evidence: number[]`) pointing to non-contradicted records in storage. |
| **`contradicted`** | Refuted claim conflicting with newer findings. | Requires explicit justification `reason` or counter-evidence `evidenceRef`. |
| **`superseded`** | Terminal historical record replaced by newer verified conclusion. | Handled via `supersedes: oldRef` or lifecycle transition. |

**Evidence Validation**: During `memory_record` and `memory_lifecycle` promotions to `supported`, `pi-mm` validates every referenced ID against storage. Nonexistent, dangling, contradicted, or superseded references are rejected.

---

## Advanced Maintenance Tools (Optional)

When `"advancedTools": true` is enabled in `.pi/settings.json` or `PI_MM_ADVANCED_TOOLS=true` is set:

| Tool | Parameters | Description |
|---|---|---|
| `memory_store` | `summary`, `level`, `type`, `lifecycle`, `episode`, `context`, `evidence`, `supersedes` | Low-level storage of raw text or mipmap envelopes (L0-L3). |
| `memory_scan` | `topic`, `level`, `lifecycle`, `project`, `subsystem`, `bbox`, `until`, `embed`, `limit` | Multidimensional search across time, text, semantics, and space. |
| `memory_traverse` | `ref`, `direction` (`down` \| `up`), `depth` | Bidirectional recursive evidence tree walker. |
| `memory_consolidate`| `mode`, `targetLevel`, `sourceRefs`, `summary`, `problem`, `context` | Synthesizes lower-level episodes into higher-level invariants. |
| `memory_lifecycle` | `ref`, `lifecycle`, `reason`, `evidenceRef` | State machine transition validator. |
| `memory_feedback` | `ref`, `utility` (`useful` \| `irrelevant` \| `obsolete`), `note` | Records retrieval utility signals. |
| `memory_think` | `key`, `extract` (`whole` \| `date` \| `text` \| `episode` \| `evidence`) | Recalls payload by ref or top-1 topic. |
| `memory_forget` | `key` | Deletes memory across primary and all roster axes (idempotent). |
| `memory_reset` | — | Enumerate and forget loop (skips `-1` sentinel). |

---

## Verification, Testing & Diagnostics

Run all quality gates and diagnostic tools using standard npm/deno scripts:

```bash
# 1. Full unit and integration test suite (196 tests across 21 suites)
npm --prefix .pi/extensions/pi-mm test

# 2. Static lint analysis (0 lint errors across 50 files)
make -C .pi/extensions/pi-mm lint

# 3. Complexity budget verification (<= 80 LOC/fn, <= 350 LOC/file)
make -C .pi/extensions/pi-mm complexity

# 4. Real C engine integration test (exercises libcorm + libstoma + libjoint)
sh .pi/extensions/pi-mm/scripts/integration-mm.sh

# 5. Native retrieval benchmark (cold vs warm latencies, P@1, recall)
npm --prefix .pi/extensions/pi-mm run benchmark

# 6. Opt-in real native embedding smoke test
deno run --allow-all --sloppy-imports .pi/extensions/pi-mm/scripts/test-real-embedding.ts
# Status: PASS (when endpoint is reachable) or SKIPPED (when unconfigured)
# Exit code: 0 on PASS/SKIPPED; 1 on genuine assertion FAIL.

# 7. Package distribution bundle (creates pi-mm-bundle.zip)
npm --prefix .pi/extensions/pi-mm run zip
```
