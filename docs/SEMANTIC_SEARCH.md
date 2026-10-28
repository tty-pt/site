# Semantic & Hybrid Search: Complete Guide

This guide explains how semantic and hybrid search work in this pure-C stack, and how to use them either via **base command-line tools** (`corm`, `libsepal`, `liblemma`, `axil-auren`) or via **turnkey HTTP services** (`axil-embed-search` for text prompts, and `axil-resume-search` for PDF documents).

---

## Table of Contents
1. [Conceptual Mental Model](#1-conceptual-mental-model)
2. [Architecture & Components](#2-architecture--components)
3. [Choosing the Right Approach](#3-choosing-the-right-approach)
4. [Prerequisite: The Embedding Daemon (`axil-auren`)](#4-prerequisite-the-embedding-daemon-axil-auren)
5. [Approach A: Base CLI Tools (`corm` + `sepal` + `lemma`)](#5-approach-a-base-cli-tools-corm--sepal--lemma)
   - [Database Syntax](#database-syntax)
   - [Ingesting Text Documents](#ingesting-text-documents)
   - [Pure Semantic Search](#pure-semantic-search)
   - [Hybrid Search (Keywords + Semantic)](#hybrid-search-keywords--semantic)
6. [Approach B: HTTP Service Modules](#6-approach-b-http-service-modules)
   - [1. `axil-embed-search` (Text Prompts & Knowledge Base)](#1-axil-embed-search-text-prompts--knowledge-base)
   - [2. `axil-resume-search` (PDF Resumes & Job Matching)](#2-axil-resume-search-pdf-resumes--job-matching)
7. [C API Integration](#7-c-api-integration)
8. [Troubleshooting & FAQ](#8-troubleshooting--faq)

---

## 1. Conceptual Mental Model

### Why Semantic Search?
Traditional databases use **lexical search** (exact keyword matching). If you search for *"socket network programming"*, a document containing *"Linux TCP/IP daemon developer"* will NOT match unless both share the exact same words.

**Semantic search** converts sentences into mathematical vectors (lists of floating-point numbers called **embeddings**) that capture the *meaning* of the text. Sentences with similar concepts end up close together in vector space.

| Search Type | Engine | How It Works | Example |
|---|---|---|---|
| **Lexical** | `liblemma` | Exact tokens, accent-sensitive (`pão ≠ pao`), prefix matching | Searching `"C99"` matches `"C99"`. |
| **Semantic** | `libsepal` | Vector cosine similarity via neural embedding models | Searching *"baking fresh bread in Portugal"* matches *"Traditional bakery making pão de queijo"*. |
| **Hybrid** | `lemma + sepal` | Lexical candidate filtering / keyword boost + semantic vector ranking | Requires mandatory keywords (e.g. `"WASM"`) while ranking candidates by similarity to a job description. |

---

## 2. Architecture & Components

The search pipeline consists of four pure-C layers:

```text
┌─────────────────────────────────────────────────────────────┐
│  Client Layer: CLI (`corm`) OR Web UI / REST (`axil`)       │
└──────────────────────────────┬──────────────────────────────┘
                               │
            ┌──────────────────┴──────────────────┐
            ▼                                     ▼
 ┌───────────────────────┐             ┌─────────────────────┐
 │  Lexical Indexing     │             │  Vector Indexing    │
 │  `liblemma`           │             │  `libsepal`         │
 │  (accent-preserving   │             │  (Hamming sketch +  │
 │   inverted tokens)    │             │   cosine rerank)    │
 └──────────┬────────────┘             └──────────┬──────────┘
            │                                     │
            │                          ┌──────────┴──────────┐
            │                          ▼                     ▼
            │               ┌────────────────────┐ ┌─────────────────┐
            │               │  `libcorm` Storage │ │  `axil-auren`   │
            │               │  (Key-Value Maps   │ │  Port 4242      │
            │               │   & Document Text) │ │  (Local Neural  │
            │               └────────────────────┘ │   Embeddings)   │
            │                                      └─────────────────┘
            ▼
 ┌───────────────────────────────────────────────────────────┐
 │ Persistent Storage (Disk / RAM)                           │
 └───────────────────────────────────────────────────────────┘
```

- **`axil-auren` (Port 4242)**: Local inference engine running dense embedding models (`bge-small` 384 dimensions, `bge-base` 768 dimensions).
- **`libsepal`**: High-performance pure-C vector similarity store. Uses two-stage ANN (Hamming sketch filter + exact cosine similarity).
- **`liblemma`**: High-performance pure-C inverted token index with accent-preserving normalization.
- **`libcorm`**: Pure-C storage engine supporting key-value and column-oriented tables.

---

## 3. Choosing the Right Approach

| Need | Recommended Tool | Interface |
|---|---|---|
| Shell scripts, batch pipelines, direct file indexing | **Base CLI Tools (`corm`)** | Command-line (`corm -p`, `corm -X`) |
| Web API or browser UI for text queries, prompts, FAQs | **`axil-embed-search`** | REST (`POST /embeddings`, `GET /search`) + UI on `:8090` |
| PDF document intake, resume parsing, job candidate matching | **`axil-resume-search`** | REST (`POST /resume`, `GET /candidates`) + UI on `:8092` |
| Custom daemon or embedded application in C | **C API (`sepal.h`, `lemma.h`, `corm.h`)** | Pure C library functions |

---

## 4. Prerequisite: The Embedding Daemon (`axil-auren`)

Both the CLI tools and the HTTP modules require the `axil-auren` embedding daemon running on port `4242`.

### Start the Daemon
```bash
AUREN_EMBED_MODEL="bge-small" \
AUREN_EMBED_PATH=~/.models/bge-small-en-v1.5.gguf \
axil -A -d -p 4242 -m libaxil-auren
```

### Verify Daemon Health
```bash
curl -s http://127.0.0.1:4242/v1/embeddings \
  -H "Content-Type: application/json" \
  -d '{"model":"bge-small","input":"ping"}'
```
*(Expected output contains: `"embedding": [...]`)*

---

## 5. Approach A: Base CLI Tools (`corm` + `sepal` + `lemma`)

Use the `corm` CLI when you want to create and query local database files directly from the terminal or shell scripts.

### 1. Configure the Environment
Tell `corm` where to find the embedding service:
```bash
export CORM_SEPAL_EMBED_URL="http://127.0.0.1:4242/v1/embeddings"
export CORM_SEPAL_EMBED_MODEL="bge-small"
```

### 2. Database Syntax
Target databases are specified as `filename.db@plugins:key_type:val_type`:
- `docs.db`: File on disk.
- `@lemma,sepal`: Attaches both the lexical (`lemma`) and vector (`sepal`) indexers.
- `:a:s`: Auto-incrementing integer key (`a`), string value (`s`).

### 3. Ingesting Text Documents (`corm -p`)
Insert records using `corm -p`:
```bash
corm -p "Zero-copy in-memory caching and RAM table indexing" docs.db@lemma,sepal:a:s
corm -p "Traditional Portuguese bakery baking warm pão de queijo" docs.db@lemma,sepal:a:s
corm -p "High-performance vector similarity search in pure C" docs.db@lemma,sepal:a:s
corm -p "Autonomous robotics navigation using lidar telemetry" docs.db@lemma,sepal:a:s
```
*Each insert automatically updates the `corm` table, generates an embedding via `axil-auren`, and indexes words in `liblemma`.*

### 4. Pure Semantic Search (`-X 'sepal'`)
Search by meaning. Notice words do not need to match:
```bash
corm -X 'sepal' --query="baking fresh bread in Portugal" docs.db@lemma,sepal:a:s
```
Output:
```text
2 0.785400 2:Traditional Portuguese bakery baking warm pão de queijo
```

Querying for robotics:
```bash
corm -X 'sepal' --query="self-driving robot sensor maps" docs.db@lemma,sepal:a:s
```
Output:
```text
4 0.762100 4:Autonomous robotics navigation using lidar telemetry
```

### 5. Hybrid Search (`-X 'lemma AND sepal'`)
Enforce a mandatory keyword via `lemma` while ranking results by semantic similarity via `sepal`:
```bash
corm -X 'lemma AND sepal' \
  --query@lemma="pão" \
  --query@sepal="breakfast pastry" \
  docs.db@lemma,sepal:a:s
```
Output:
```text
2 0.785400 2:Traditional Portuguese bakery baking warm pão de queijo
```

---

## 6. Approach B: HTTP Service Modules

For web applications, browser dashboards, or microservices, use the pre-built Axil modules.

### 1. `axil-embed-search` (Text Prompts & Knowledge Base)

Located in `external/axil-embed-search/`. Ideal for raw text, documentation search, and chat prompt retrieval.

#### Run the Service
```bash
./external/axil-embed-search/run.sh
```
*Starts on `http://localhost:8090` (custom port via `PORT=8099`).*

#### Ingest a Text Document
```bash
curl -s -X POST http://localhost:8090/embeddings \
  -H "Content-Type: application/json" \
  -d '{"prompt": "High-throughput asynchronous I/O using Linux io_uring"}'
```

#### Query Documents
```bash
curl -s "http://localhost:8090/search?q=fast+disk+storage+techniques&mode=hybrid" | jq
```

#### Interactive Browser UI
Open **`http://localhost:8090/`** in your browser for the search dashboard.

---

### 2. `axil-resume-search` (PDF Resumes & Job Matching)

Located in `external/axil-resume-search/`. Specifically designed for PDF file ingestion, automatic text extraction under the hood, and matching candidates to natural-language job opportunity descriptions.

#### How It Works:
1. **Uploads**: Accepts PDF files via `multipart/form-data` (browser file input), raw binary `application/pdf`, or base64 JSON.
2. **Text Extraction**: Runs `pdftotext -layout <file> -` under the hood via `popen()`, extracting the complete document text into memory.
3. **Physical Storage**: Writes the uploaded PDF to disk (`uploads/resume_<ref>_<filename>.pdf`).
4. **Vector Embedding**: Computes dense embeddings with `axil-auren` on port 4242 and stores vectors in `libsepal`.
5. **Job Matching**: Users pass natural language job opportunity descriptions (`GET /candidates?job=...`). The system ranks candidates by match score and provides PDF download links.

#### Run the Service
```bash
./external/axil-resume-search/run.sh
```
*Starts on `http://localhost:8092` (custom port via `PORT=8095`).*

#### Upload a PDF Resume
```bash
curl -s -X POST http://localhost:8092/resume \
  -F "file=@external/axil-resume-search/test_resumes/paulo_lead_frontend_systems_architect.pdf;type=application/pdf" | jq
```
Response:
```json
{
  "ok": true,
  "ref": 1,
  "candidate": "Paulo Quirino",
  "file_name": "paulo_lead_frontend_systems_architect.pdf",
  "file_path": "uploads/resume_1_paulo_lead_frontend_systems_architect.pdf",
  "file_size": 9746,
  "text_len": 4120,
  "snippet": "Paulo Quirino - Lead Frontend Engineer & Systems Architect...",
  "file_url": "/resume/file?ref=1"
}
```

#### Search Candidates by Job Opportunity
```bash
curl -s "http://localhost:8092/candidates?job=Lead+Frontend+Engineer+with+React+and+WASM+systems+experience&mode=hybrid" | jq
```

#### Download / View Original Stored PDF
```bash
curl -O -J "http://localhost:8092/resume/file?ref=1"
```

#### Interactive Browser UI
Open **`http://localhost:8092/`** in your browser for the full interactive matching dashboard.

---

## 7. C API Integration

To embed semantic search into your own C module, link `-laxil -lxylem -lcorm -lsepal -llemma`:

```c
#include <ttypt/sepal.h>
#include <ttypt/lemma.h>
#include <ttypt/corm.h>

/* 1. Initialize engines */
sepal_configure_embeddings("http://127.0.0.1:4242/v1/embeddings", "bge-small", NULL);
int err = 0;
sepal_vecstore_t *vs = sepal_open(NULL, &err); /* NULL = in-memory */
lemma_db_t *ldb = lemma_open(0);
uint32_t text_map = corm_open(NULL, NULL, CM_STR, CM_STR, 1023, 0);

/* 2. Ingest document */
uint32_t doc_id = 1;
const char *text = "Low-latency Linux networking daemon in pure C99";
float *vec = NULL;
size_t dim = 0;
if (sepal_embed_fetch(text, &vec, &dim) == 0) {
    sepal_put(vs, doc_id, vec, dim);
    free(vec);
}
lemma_index_ref(ldb, "text", doc_id, text);
corm_put(text_map, "1", text);

/* 3. Query with natural language */
float *qvec = NULL;
size_t qdim = 0;
if (sepal_embed_fetch("socket server programmer", &qvec, &qdim) == 0) {
    sepal_hit_t hits[10];
    size_t count = sepal_search(vs, qvec, qdim, 10, 0.0f, 0, hits);
    for (size_t i = 0; i < count; i++) {
        printf("Hit Ref: %u, Similarity Score: %.4f\n", hits[i].ref, hits[i].score);
    }
    free(qvec);
}

/* 4. Cleanup */
sepal_close(vs);
lemma_close(ldb);
corm_close(text_map);
```

---

## 8. Troubleshooting & FAQ

### 1. `axil-auren: connection refused` on port 4242
- **Cause**: The embedding daemon is not running.
- **Fix**: Launch `axil-auren` using the command in [Section 4](#4-prerequisite-the-embedding-daemon-axil-auren).

### 2. Can I use external embedding APIs (e.g. OpenAI)?
- Yes. `libsepal` and `axil-auren` implement the standard OpenAI `/v1/embeddings` endpoint format (`{"model": "...", "input": "..."}`). Point `CORM_SEPAL_EMBED_URL` to any OpenAI-compatible provider.

### 3. How are accents handled?
- `liblemma` preserves diacritics strictly: `pão ≠ pao`.
- `libsepal` converts text into semantic vector embeddings, capturing language meaning regardless of spelling variations.

### 4. Can embeddings classify short commands (intent detection)?
- Yes, and it was measured against a generative judge for several decisions: embed a small set of labelled example sentences once, embed each incoming message, and take the nearest example's label by cosine similarity (vectors from `axil-auren` are unit length, so a dot product is the cosine). Require a minimum similarity and a margin over the runner-up label before acting. This is deterministic, order-free and ~15 ms per message. `pi-quest` uses it for operator steering, quest-vs-conversation and difficulty (`.pi/extensions/pi-quest/src/judge/nearest.ts`, `src/domain/intent.ts`) and `pi-mm` for deciding which conversational turns to store (`.pi/extensions/pi-mm/src/judge/nearest.ts`).
- **Measured and adopted everywhere.** Embeddings beat the local generative judge across decisions with zero wrong state-changing verdicts. For plan verification, decomposed fine-grained categories with coarse grouping and advisory reporting achieve 0 false blocks and 86.4% gap detection on held-out corpora. Consequently, generative LLM judging has been fully deprecated in favor of fast in-memory embedding classification via `POST /v1/classify` (~10–20ms) with deterministic offline fallbacks. Expect to add examples for custom domain phrasing. The harness in `.pi/extensions/pi-quest/tests/eval/` measures performance for any labelled corpus.
- The `bge-*` models are English-only; add exemplars in your own language for better recall.


### 5. Where are uploaded PDFs stored?
- In `axil-resume-search`, uploaded files are stored physically under `uploads/` (`uploads/resume_<id>_<filename>.pdf`). Stored PDFs can be directly downloaded or viewed via `GET /resume/file?ref=<id>`.
