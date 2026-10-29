# Multi-Axis Memory Fusion: Dense Vectors, Symbolic Epistemology, and Corm Axes

This document details the division of responsibilities and architectural fusion between **`axil-auren`** (neural inference engine) and **`pi-mm`** (persistent multi-resolution episodic memory space), as well as the spatial-relational coordinate space of **`libcorm`** (`joint`, `lemma`, `sepal`, `islet`).

---

## 1. Executive Summary & Separation of Concerns

Memory retrieval in advanced AI coding agents cannot rely purely on semantic vector similarity (k-NN cosine distance) alone:
1. **Neural embeddings are blind to exact symbols**: A dense embedding model struggles with exact symbol names, hexadecimal hashes, error codes (`0x8cc2265ca4ed`, `TS2304`), and exact file paths.
2. **Neural embeddings are epistemically agnostic**: A contradicted hypothesis or an obsolete architectural decision from 6 months ago may have a $0.98$ cosine similarity to the user's prompt. Without symbolic grounding, pure vector search produces confident hallucinations based on stale state.
3. **`axil-auren` is not a kitchen sink**: `axil-auren` is an **inference and embedding engine** (GGUF neural network forward passes, AVX/NEON SIMD dot products, dense embeddings, and fast stateless classification). It does **not** host every present and future combination of orthogonal axes.
4. **`libcorm` is the multi-axis coordinate space**: `libcorm` coordinates orthogonal dimensions (`lemma` for lexical BM25, `sepal` for vector storage, `joint` for time and multi-index intersection, `islet` for 2D/3D spatial bounding boxes).
5. **`pi-mm` is the cognitive and epistemic ranker**: `pi-mm` fuses the candidate sets returned across axes using Reciprocal Rank Fusion (RRF), then applies multi-axis symbolic re-ranking (epistemic lifecycle multipliers, evidence DAG traversal, mipmap abstraction levels, project/subsystem scope bonuses, and code noise penalties).

---

## 2. The Orthogonal Axes: `lemma`, `sepal`, `joint`, and `islet`

In `libcorm`, databases do not use rigid tabular relational schemas; they are structured as coordinate spaces governed by pluggable **axes**:

| Axis Name | Role | Operational Mechanics |
|-----------|------|------------------------|
| **`lemma`** | Lexical FTS & Inverted Index | Exact token matching, stemming, prefix searching, and BM25 term weighting. Handles exact function names, syntax tokens, and identifiers. |
| **`sepal`** | Dense Vector Space | Stores 384-dimensional dense vectors on disk (`.sepal`). Computes cosine similarity against query vectors. Retrieves vectors from `axil-auren` via HTTP during indexing. |
| **`joint`** | Temporal & Multi-Axis Intersection | Provides Cartesian coordinate joins and temporal mipmap windows (L0 point-in-time, L1 daily, L2 monthly, L3 epoch). Intersects lexical, spatial, and vector filters in pure C. |
| **`islet`** | Spatial Bounding Boxes | Evaluates 2D/3D bounding boxes and geometric partitions for spatial-relational queries. |

### Why `axil-auren` Must Not Host All Combinations of Axes
If `axil-auren` attempted to host every combination of `lemma`, `sepal`, `joint`, `islet`, and potential future axes (e.g., AST syntax trees, call graph topologies, audio fingerprints), it would break modularity. 

Instead:
- **`axil-auren`** provides the **dense vector scores** via `sepal_embed_fetch` / `/v1/embeddings` and stateless cosine scoring via `POST /v1/classify`.
- **`libcorm`** coordinates the storage and intersection of axes on disk.
- **`pi-mm`** governs the epistemological fusion across all axes.

---

## 3. Two-Stage Multi-Axis Fusion Pipeline in `pi-mm`

```
                         User Inquiry / Context Hook
                                      │
              ┌───────────────────────┴───────────────────────┐
              ▼                                               ▼
       Lexical Axis (FTS)                              Semantic Axis (ANN)
    liblemma BM25 query (corm)                     axil-auren / libsepal (ANN)
              │                                               │
              └───────────────────────┬───────────────────────┘
                                      ▼
                             Stage 1: Fusion
                           fuseRankingsRRF(...)
                        Reciprocal Rank Fusion (RRF)
                                      │
                                      ▼
                         Stage 2: Symbolic Re-Ranking
                           computeRankingScore(...)
             ┌─────────────────────────────────────────────────┐
             │ • Epistemic Lifecycle State Multipliers         │
             │ • Graph Provenance / Citation DAG Bonus         │
             │ • Flashbulb Salience Boost (isAnchor)           │
             │ • Hierarchical Mipmap Level Selection (L0-L3)   │
             │ • Project / Subsystem Scope Match               │
             │ • Empirical Net Utility Feedback                │
             │ • Conversational Code / Docstring Penalty       │
             └─────────────────────────────────────────────────┘
                                      │
                                      ▼
                           Final Ranked Memories
```

### Stage 1: Scale-Invariant Reciprocal Rank Fusion (`fuseRankingsRRF`)
Lexical BM25 yields unbounded positive floats, whereas semantic ANN yields cosine similarities in $[-1.0, 1.0]$. Directly summing these values causes distortion.

`pi-mm` resolves this via **Reciprocal Rank Fusion (RRF)** (`src/tools/ranking.ts:92-140`):
$$\text{RRF Score}(d) = \sum_{m \in \{\text{lexical}, \text{semantic}\}} \frac{1}{k + \text{rank}_m(d)}$$

The smoothing constant $k$ is dynamically scaled via `adaptiveRrfK(candidateCount)`:
```ts
export function adaptiveRrfK(candidateCount: number): number {
  if (candidateCount <= 1) return 10;
  return Math.max(10, Math.min(60, candidateCount * 2));
}
```
When a record matches both exact tokens (`lemma`) and conceptual paraphrase (`sepal`), its origin is marked as `"both"` and it receives the additive reciprocal rank boost.

---

### Stage 2: Symbolic & Epistemic Re-Ranking (`computeRankingScore`)

Once RRF establishes candidate relevance, `pi-mm` re-ranks candidates across symbolic dimensions (`src/tools/ranking.ts:149-183`):

#### 1. Epistemic Lifecycle Multipliers
Every memory record possesses a verified epistemic lifecycle state:
- `supported` ($\times 1.0$): Ground truth verified by concrete test execution and active evidence.
- `tested` ($\times 0.95$): Observed to work in execution, pending formal consolidation.
- `observed` ($\times 0.90$): Raw empirical observation.
- `hypothesized` ($\times 0.70$): Speculative explanation.
- `contradicted` ($\times 0.10$): Refuted by later tests (retained solely for negative learning).
- `superseded` ($\times 0.05$): Replaced by newer truth (pruned from standard recall).

#### 2. Evidence DAG Provenance Bonus
Memories linked into the evidence hierarchy (`evidence: number[]`) receive a provenance bonus:
```ts
if (env.evidence && env.evidence.length > 0) {
  score *= 1.05;
}
```

#### 3. Salience / Flashbulb Anchors
Standing directives and fundamental architectural rules are tagged as flashbulb memories:
```ts
if (env.isAnchor) {
  score *= 1.25; // 25% salience boost
}
```

#### 4. Scope and Subsystem Compatibility
Candidates matching the active operational scope receive targeted alignment boosts:
```ts
if (crit.project && env.context?.project === crit.project) score *= 1.05;
if (crit.subsystem && env.context?.subsystem === crit.subsystem) score *= 1.05;
```

#### 5. Conversational Noise Pruning
When answering natural-language conversational queries (e.g., *"Do you remember what my grandfather said?"*), code artifacts and raw C function docstrings matching incidental terms receive an $80\%$ penalty:
```ts
if (crit.mipmap !== "L0" && isCodeOrDocArtifact(cand, env)) {
  score *= 0.2;
}
```

---

## 4. Summary Matrix

| Capability | Component Responsible | Protocol / Seam |
|---|---|---|
| Vector Embedding Generation | `axil-auren` | `POST /v1/embeddings` |
| Stateless Cosine Margin Scoring | `axil-auren` | `POST /v1/classify` |
| Lexical Inverted Index (BM25) | `libcorm` (`liblemma`) | CLI / `cormd` socket |
| Vector Persistence & Indexing | `libcorm` (`libsepal`) | `.sepal` binary index |
| Multi-Axis Coordinate Intersection | `libcorm` (`joint`) | `-X joint,lemma,sepal` |
| Cross-Axis Fusion (RRF) | `pi-mm` | `fuseRankingsRRF()` |
| Epistemic & Symbolic Re-Ranking | `pi-mm` | `computeRankingScore()` |
| Evidence Lineage Traversal | `pi-mm` | `src/tools/traverse.ts` |
