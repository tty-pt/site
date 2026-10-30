# LOCAL_LLM.md — Local Inference & Semantic Search with Axil-Auren

Turnkey local LLM inference, OpenAI-compatible streaming chat, dense vector embeddings,
and fast in-memory semantic classification running as an Axil service (`libaxil-auren.so`).

---

## 1. Quickstart

Start the local `axil-auren` engine on port 4242:

```bash
/home/quirinpa/axil-auren/scripts/auren-server
```

Test the API endpoints:

```bash
# Check loaded models:
curl -s http://localhost:4242/v1/models

# Fast In-Memory Classification (<20ms, zero float vectors over TCP):
curl -s -X POST http://localhost:4242/v1/classify \
  -H "Content-Type: application/json" \
  -d '{"model":"bge-base","text":"run test suite","labels":{"verify":["run unit tests","make test"],"other":["edit file","update docs"]}}'

# Dense vector embeddings:
curl -s -X POST http://localhost:4242/v1/embeddings \
  -H "Content-Type: application/json" \
  -d '{"model":"bge-base","input":"Semantic search with Axil"}'
```

---

## 2. In-Memory Semantic Classification (`POST /v1/classify`)

Autonomous workflow tools (`pi-quest` and `pi-mm`) use `POST /v1/classify` for all semantic decisions.
Instead of streaming 500 KB float vector arrays across loopback TCP and computing cosines in client scripts:
- Clients send probe text + exemplar label strings.
- `axil-auren` embeds inputs in C using `bge-base` and caches exemplar vectors in-memory.
- SIMD unit-vector dot products compute label affinities and runner-up margins.
- Returns compact JSON (~150 bytes) in ~10–20ms with zero persistent disk mutations or cleanup required.

---

## 3. Persistent Multi-Tenant Collections (`/v1/collections/*`)

`axil-auren` supports multi-tenant persistent semantic indexing powered by `libsepal` and `libcorm`:
- `POST   /v1/collections/{name}/documents`: Ingests and persists document text and vectors into `{name}.corm` and `{name}.sepal`.
- `GET    /v1/collections/{name}/search?q=...&limit=10`: Returns ranked results with scores.
- `DELETE /v1/collections/{name}/documents/{id}`: Deletes document records and vectors.
- Strict isolation: collection names are validated to prevent directory traversal, and tenant vector spaces are strictly partitioned.

---

## 4. Deprecated Decision Endpoints (`systemone` & `systemzero`)

> **DEPRECATION NOTICE**: Heavyweight generative LLM judging (`systemone` and `systemzero` via `olmoe`/`qwen`) is deprecated in favor of `POST /v1/classify`. The compiler warns when linking against these functions, and client extensions rely on fast embedding classification with deterministic local fallbacks (exact commands, `/quest`, `@remember`, structural heuristics).

---

## 5. Semantic Classification Decision Surfaces & Audit (pi-mm & pi-quest)

### Active Classification Surfaces
Both `pi-mm` and `pi-quest` leverage `POST /v1/classify` via `bge-base` for fast, zero-disk, SIMD-accelerated decisions:

| Surface | Extension | Role | Kinds / Labels | Measured Latency |
| :--- | :--- | :--- | :--- | :--- |
| **Conversational Capture** | `pi-mm` | Write-path: decides if a conversational statement is worth storing | `remember` vs `skip` | ~15 ms |
| **Recall Intent** | `pi-mm` | Read-path: detects if user prompt queries past decisions or lore | `recall_past` vs `standalone` | ~17 ms |
| **Quest Creation Intent** | `pi-quest` | Determines if user input requests a substantive software engineering task | `quest` vs `conversation` | ~18 ms |
| **Quest Difficulty Tiering** | `pi-quest` | Automatically tiers incoming quests into execution complexity | `trivial`, `standard`, `complex` | ~18 ms |
| **Quest Kind Gating** | `pi-quest` | Distinguishes analytical research tasks from code implementation tasks | `standard` vs `analysis` | ~16 ms |
| **Operator Steering Triage** | `pi-quest` | Classifies mid-flight user steering during quest execution | `advance`, `abort`, `constraint`, `subquest_*`, `refinement` | ~15 ms |
| **Plan Step Quality** | `pi-quest` | Verifies implementation plan steps against verification exemplars | `verify`, `edit`, `investigate`, etc. | ~16 ms |

### Audited Candidate Surfaces (Implementation Roadmap & Live Metrics)

The following decision points currently rely on rigid regexes or generative prompts and represent prime candidates for `POST /v1/classify`:

1. **Standing Directives & Operational Invariants (`isStandingDirective`)** — *`pi-mm` (Tier 1, Highest ROI)*:
   - Replaces English-only regex `/(?:always\s+remember|never\s+use|our\s+rule\s+is|standing\s+rule|invariant:)/i` with `["standing_directive", "transient_task"]`.
   - Empirically verified on live `axil-auren`: catches natural English (*"Make sure we never write raw SQL in handlers"*, similarity 0.937, margin +0.349) and Portuguese (*"Regra aqui: em todo arquivo novo, use a licença BSD-2-Clause"*, similarity 0.809) with zero false positives on transient tasks.
2. **Open-Ended Overview vs. Targeted Recall (`isDirectMemoryQuestion`)** — *`pi-mm` (Tier 1, High ROI)*:
   - Replaces fragile regex with `["overview_query", "specific_query"]`.
   - Accurately routes open-ended synthesis requests to multi-resolution timeline mipmaps rather than running empty lexical keyword scans.
3. **Fine-Grained Quest Kinds (`classifyQuestKind`)** — *`pi-quest` (Tier 2, High Architectural Value)*:
   - Expands binary `standard`/`analysis` to `["feature", "bugfix", "refactor", "docs", "analysis"]`.
   - Dynamically tailors verification requirements (e.g. bugfixes require regression test commands; docs quests do not require binary test receipts).
4. **Mid-Quest Tangent Triage (`triageInQuestRequest`)** — *`pi-quest` (Tier 2, Medium ROI)*:
   - Classifies non-sigil remarks during an active quest into `["in_scope_refinement", "tangent_subquest", "unrelated_query"]` to prevent plan bloat.
5. **Consolidation Candidate Pruning (`consolidate`)** — *`pi-mm` (Tier 3, Token Optimization)*:
   - Pre-filters candidate episode clusters (`"generalizable_invariant"` vs `"narrow_bugfix"`) to prune dead ends before spending tokens on generative synthesis.

