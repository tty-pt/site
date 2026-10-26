# axil-embed-search

A high-performance pure-C [Axil](https://github.com/tty-pt/axil) module providing vector embedding storage and hybrid search. It interfaces with an external `axil-auren` instance running local GGUF models for dense vector embeddings, and stores them in [libsepal](https://github.com/tty-pt/libsepal) (approximate nearest neighbor vector search) and [liblemma](https://github.com/tty-pt/liblemma) (accent-sensitive lexical full-text search).

---

## Architecture

- **HTTP Engine**: Pure-C Axil daemon request dispatch via `axil_register_handler`.
- **Embeddings Backend**: Interoperates with `axil-auren` (`http://127.0.0.1:4242/v1/embeddings`) using OpenAI-compatible JSON payloads (`bge-small` 384-dimensional dense vectors).
- **Vector Search Engine**: [libsepal](https://github.com/tty-pt/libsepal) two-stage ANN pipeline (Hamming sketch prefilter + exact cosine rerank).
- **Lexical Search Engine**: [liblemma](https://github.com/tty-pt/liblemma) inverted index with accent-preserving token folding (`pão ≠ pao`).
- **Prompt Storage**: [libcorm](https://github.com/tty-pt/libcorm) persistent string map linking document IDs (`ref`) to raw prompt text.

---

## Prerequisites

1. Ensure the `axil-auren` embedding daemon is running on port `4242`:
   ```bash
   curl -s http://127.0.0.1:4242/v1/embeddings \
     -H "Content-Type: application/json" \
     -d '{"model":"bge-small","input":"ping"}'
   ```
   *(Expected response: `{"data":[{"object":"embedding","embedding":[...]}]}`)*

2. Installed runtime libraries: `libaxil.so`, `libxylem.so`, `libcorm.so`, `libsepal.so`, `liblemma.so`, `libjson-c.so`.

---

## Build & Run

### 1. Turnkey Launcher (Recommended)
Simply run the turnkey runner script (auto-builds if needed and launches on port 8090):
```bash
./external/axil-embed-search/run.sh
```
Or customize the port:
```bash
PORT=8099 ./external/axil-embed-search/run.sh
```

### 2. Manual Build & Run
Compile the shared library:
```bash
make -C external/axil-embed-search
```
This produces `lib/libaxil-embed-search.so` (and symlink `lib/axil-embed-search.so`).

Run the Axil server with the module loaded:
```bash
axil -d -p 8090 -m /home/quirinpa/site/external/axil-embed-search/lib/libaxil-embed-search
```
*(Note: Axil automatically appends `.so` to module paths, so pass the path without the trailing `.so` extension).*

---

## REST Endpoints & `curl` Usage

### 1. Ingest a Document (`POST /embeddings` or `POST /api/embeddings`)

Send a JSON payload containing the prompt/text to embed. The module contacts `axil-auren` on port `4242`, generates the embedding, and indexes the document.

#### Ingest with Auto-Generated ID:
```bash
curl -s -X POST http://127.0.0.1:8090/embeddings \
  -H "Content-Type: application/json" \
  -d '{"prompt": "High-performance vector similarity search in pure C"}'
```

**Response**:
```json
{
  "ok": true,
  "ref": 1,
  "dim": 384,
  "prompt": "High-performance vector similarity search in pure C"
}
```

#### Ingest with Custom ID / Ref:
```bash
curl -s -X POST http://127.0.0.1:8090/embeddings \
  -H "Content-Type: application/json" \
  -d '{"prompt": "Traditional Portuguese bakery baking warm pão de queijo", "ref": 102}'
```

**Response**:
```json
{
  "ok": true,
  "ref": 102,
  "dim": 384,
  "prompt": "Traditional Portuguese bakery baking warm pão de queijo"
}
```

Supported JSON keys for the input string: `"prompt"`, `"text"`, or `"input"`.

---

### 2. Query Documents (`GET /search` or `GET /api/search`)

Retrieve stored documents using semantic vector similarity, lexical full-text matching, or combined hybrid ranking.

#### Query Parameters
- `q` (or `query`): Search query string *(required)*.
- `mode`: Search strategy: `sepal` (default), `lemma`, or `hybrid` *(optional)*.
- `k` (or `limit`): Maximum number of results to return (default: `10`, max: `100`) *(optional)*.
- `min_sim`: Minimum cosine similarity score threshold (default: `0.0`) *(optional)*.

---

#### A. Semantic Vector Search (`mode=sepal`)
Embeds the query string via `axil-auren` and searches the `libsepal` vector index by cosine similarity:
```bash
curl -s "http://127.0.0.1:8090/search?q=fast+memory+storage+techniques&mode=sepal"
```

**Response**:
```json
{
  "ok": true,
  "query": "fast memory storage techniques",
  "mode": "sepal",
  "count": 2,
  "hits": [
    {
      "ref": 1,
      "score": 0.8058,
      "prompt": "High-performance vector similarity search in pure C"
    },
    {
      "ref": 102,
      "score": 0.3427,
      "prompt": "Traditional Portuguese bakery baking warm pão de queijo"
    }
  ]
}
```

---

#### B. Lexical Search (`mode=lemma`)
Searches the `liblemma` inverted index using accent-preserving word token prefix matching:
```bash
curl -s "http://127.0.0.1:8090/search?q=pão&mode=lemma"
```

**Response**:
```json
{
  "ok": true,
  "query": "pão",
  "mode": "lemma",
  "count": 1,
  "hits": [
    {
      "ref": 102,
      "score": 1.0,
      "prompt": "Traditional Portuguese bakery baking warm pão de queijo"
    }
  ]
}
```

---

#### C. Hybrid Search (`mode=hybrid`)
Combines lexical candidate discovery with semantic vector ranking, applying an additional relevance boost to items matching both criteria:
```bash
curl -s "http://127.0.0.1:8090/search?q=baking+fresh+bread+in+portugal&mode=hybrid"
```

**Response**:
```json
{
  "ok": true,
  "query": "baking fresh bread in portugal",
  "mode": "hybrid",
  "count": 2,
  "hits": [
    {
      "ref": 102,
      "score": 0.7762,
      "prompt": "Traditional Portuguese bakery baking warm pão de queijo",
      "lexical_match": true
    },
    {
      "ref": 1,
      "score": 0.3922,
      "prompt": "High-performance vector similarity search in pure C",
      "lexical_match": false
    }
  ]
}
```

---

## Interactive Web Form (`GET /` or `GET /embed-search`)

In addition to `curl` API access, the module serves an interactive HTML UI directly in the browser:
- Open `http://localhost:8090/` or `http://localhost:8090/embed-search` in your web browser.
- **Ingest Document**: Type any prompt text, optionally specify a document ID, and click **Ingest** to generate embeddings and index the document with instant JSON status feedback.
- **Search Documents**: Enter search queries, select a mode (**Semantic**, **Lexical**, or **Hybrid**), set a result limit, and view ranked hit cards with similarity scores, document IDs, and text snippets.

---

## Environment Configuration

Configure custom endpoints and storage locations via environment variables:

| Variable | Default | Description |
| :--- | :--- | :--- |
| `AUREN_EMBED_URL` | `http://127.0.0.1:4242/v1/embeddings` | Target URL for the OpenAI-compatible embedding service |
| `AUREN_EMBED_MODEL`| `bge-small` | Model identifier sent in embedding request bodies |
| `SEPAL_DB_PATH` | *(empty = in-memory)* | Path to persistent disk file for the `libsepal` vector store |

---

## Running the Automated Test Suite

Run the full end-to-end integration test suite:
```bash
make -C external/axil-embed-search test
```
Or directly:
```bash
./external/axil-embed-search/test.sh
```
The test suite spins up an ephemeral Axil server on an unused port, tests all error conditions (empty body, bad JSON, missing prompt/query), ingests documents via `POST /embeddings`, verifies 384-dimensional vector creation, and validates all three search modes (`sepal`, `lemma`, `hybrid`).

---

## Sample Test Documents & Queries

### Ingest Batch via `curl`
```bash
# Systems & CS
curl -s -X POST http://localhost:8090/embeddings -H "Content-Type: application/json" -d '{"ref": 1, "prompt": "High-performance vector similarity search in pure C with zero runtime dependencies"}'
curl -s -X POST http://localhost:8090/embeddings -H "Content-Type: application/json" -d '{"ref": 2, "prompt": "Zero-copy in-memory caching and RAM table indexing for low-latency databases"}'
curl -s -X POST http://localhost:8090/embeddings -H "Content-Type: application/json" -d '{"ref": 3, "prompt": "Asynchronous event loop driving non-blocking socket I/O over epoll and kqueue"}'
curl -s -X POST http://localhost:8090/embeddings -H "Content-Type: application/json" -d '{"ref": 4, "prompt": "Quantized local LLM inference running GGUF weights on AVX2 CPU instructions"}'

# Culinary & Bakery (accent test)
curl -s -X POST http://localhost:8090/embeddings -H "Content-Type: application/json" -d '{"ref": 10, "prompt": "Traditional Portuguese bakery baking warm pão de queijo and pastéis de nata"}'
curl -s -X POST http://localhost:8090/embeddings -H "Content-Type: application/json" -d '{"ref": 11, "prompt": "Artisan sourdough loaf with a crispy crust made from wild yeast starter"}'
curl -s -X POST http://localhost:8090/embeddings -H "Content-Type: application/json" -d '{"ref": 12, "prompt": "Freshly brewed espresso with creamy milk foam and cinnamon powder"}'
curl -s -X POST http://localhost:8090/embeddings -H "Content-Type: application/json" -d '{"ref": 13, "prompt": "Wood-fired Neapolitan pizza topped with buffalo mozzarella and fresh basil"}'

# Music & Acoustics
curl -s -X POST http://localhost:8090/embeddings -H "Content-Type: application/json" -d '{"ref": 20, "prompt": "Vintage 1960s electric guitar with humbucker pickups through a warm tube amplifier"}'
curl -s -X POST http://localhost:8090/embeddings -H "Content-Type: application/json" -d '{"ref": 21, "prompt": "Classical Spanish flamenco guitar strung with high-tension nylon strings"}'
curl -s -X POST http://localhost:8090/embeddings -H "Content-Type: application/json" -d '{"ref": 22, "prompt": "Upright acoustic grand piano tuned to concert pitch 440 Hz in a resonant hall"}'
curl -s -X POST http://localhost:8090/embeddings -H "Content-Type: application/json" -d '{"ref": 23, "prompt": "Tenor saxophone playing smooth melancholic jazz melodies in a smoky club"}'
```

### Try Queries
- **Semantic Vector Match**:
  `curl -s "http://localhost:8090/search?q=fast+RAM+storage+techniques&mode=sepal"`
  *(Finds doc 2 conceptually even without matching keywords).*
- **Lexical Accent Match**:
  `curl -s "http://localhost:8090/search?q=pão&mode=lemma"`
  *(Finds doc 10 with accent-sensitive match).*
- **Hybrid Search**:
  `curl -s "http://localhost:8090/search?q=crispy+Italian+dough&mode=hybrid"`
  *(Ranks pizza and sourdough matches).*
