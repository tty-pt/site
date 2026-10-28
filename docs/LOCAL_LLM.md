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
