# Semantic & Hybrid Search

This document explains how to set up and run semantic and hybrid search using `axil-auren`, `libsepal`, and `liblemma`.

---

## Table of Contents
1. [Quick Start](#1-quick-start)
2. [Search Modes](#2-search-modes)
   - [Semantic Search](#semantic-search)
   - [Hybrid Search (Keyword + Vector)](#hybrid-search-keyword--vector)
   - [Database Syntax](#database-syntax)
3. [C API Example](#3-c-api-example)
4. [Caching & Performance Tuning](#4-caching--performance-tuning)
5. [Installation & Model Setup](#5-installation--model-setup)
   - [Download Embedding Model](#download-embedding-model)
   - [Install Packages](#install-packages)
   - [Build from Source](#build-from-source)
6. [HTTP Service: axil-embed-search Module](#6-http-service-axil-embed-search-module)

---

## 1. Quick Start

### 1. Start the Embedding Daemon
Run `axil-auren` on port `4242` with your preferred embedding model:
- **`bge-small`**: Fast & ultra-lightweight (384-dimensional, 37 MB)
- **`bge-base`**: Higher retrieval accuracy (768-dimensional, 118 MB)

```bash
# Option A: Fast & lightweight (bge-small, default)
AUREN_EMBED_MODEL="bge-small" \
AUREN_EMBED_PATH=~/.models/bge-small-en-v1.5.gguf \
axil -A -d -p 4242 -m libaxil-auren

# Option B: High accuracy (bge-base)
# AUREN_EMBED_MODEL="bge-base" \
# AUREN_EMBED_PATH=~/.models/bge-base-en-v1.5.gguf \
# axil -A -d -p 4242 -m libaxil-auren
```

Verify that the daemon answers:
```bash
# Query bge-small (384 dims):
curl -s http://127.0.0.1:4242/v1/embeddings \
  -H "Content-Type: application/json" \
  -d '{"model":"bge-small","input":"test string"}'

# Or query bge-base (768 dims):
curl -s http://127.0.0.1:4242/v1/embeddings \
  -H "Content-Type: application/json" \
  -d '{"model":"bge-base","input":"test string"}'
```

### 2. Configure Client Environment
Point `corm` / `libsepal` to the embedding endpoint:

```bash
export CORM_SEPAL_EMBED_URL="http://127.0.0.1:4242/v1/embeddings"
# Choose "bge-small" (384-d, fastest) or "bge-base" (768-d, higher accuracy):
export CORM_SEPAL_EMBED_MODEL="bge-base"
```

### 3. Ingest Documents
Insert documents using `corm -p`. The `@lemma,sepal` suffix indexes each row with both lexical tokens (`liblemma`) and semantic vectors (`libsepal`):

```bash
corm -p "Zero-copy in-memory caching and RAM table indexing" docs.db@lemma,sepal:a:s
corm -p "Traditional Portuguese bakery baking warm pão de queijo" docs.db@lemma,sepal:a:s
corm -p "High-performance vector similarity search in pure C" docs.db@lemma,sepal:a:s
```

### 4. Run a Query
Query the vector index with `-X 'sepal'`:

```bash
corm -X 'sepal' --query="fast memory storage techniques" docs.db@lemma,sepal:a:s
```

Output:
```text
1 0.814200 1:Zero-copy in-memory caching and RAM table indexing
3 0.692100 3:High-performance vector similarity search in pure C
```

---

## 2. Search Modes

### Semantic Search
Searches by vector similarity. Matches documents with similar meaning even if words differ:

```bash
corm -X 'sepal' --query="baking fresh bread in Portugal" docs.db@lemma,sepal:a:s
```

Output:
```text
2 0.785400 2:Traditional Portuguese bakery baking warm pão de queijo
```

### Hybrid Search (Keyword + Vector)
Combines strict lexical filtering with vector ranking. This example requires the word `"pão"` (preserving accents) while ranking matches by similarity to `"breakfast pastry"`:

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

### Database Syntax
The string `docs.db@lemma,sepal:a:s` defines the target:
- `docs.db`: database file name on disk.
- `@lemma,sepal`: active index plugins (`lemma` for lexical search, `sepal` for vector search).
- `:a`: primary key mode (`a` = auto-incrementing integer ID).
- `:s`: value schema (`s` = UTF-8 string).

---

## 3. C API Example

To use `libsepal` directly from C code:

```c
#include <stdio.h>
#include <stdlib.h>
#include <ttypt/sepal.h>

int main(void) {
    int err = 0;

    // 1. Open persistent vector store
    sepal_vecstore_t *vs = sepal_open("store.db", &err);
    if (!vs) return 1;

    // 2. Set embedding daemon URL
    sepal_configure_embeddings("http://127.0.0.1:4242/v1/embeddings", "bge-small", NULL);

    // 3. Store text items (auto-embedded via the daemon)
    rec_axis_store(vs, NULL, 1, "Zero-copy in-memory caching and RAM table indexing");
    rec_axis_store(vs, NULL, 2, "Traditional Portuguese bakery baking warm pão de queijo");
    rec_axis_store(vs, NULL, 3, "High-performance vector similarity search in pure C");

    // 4. Search using a query vector (384 dimensions for bge-small)
    // Here we pass a sample query vector:
    float query_vec[4] = {0.85f, 0.15f, 0.0f, 0.0f};
    sepal_hit_t hits[3];
    size_t count = sepal_search(vs, query_vec, 4, 3, 0.0f, 0, hits);

    for (size_t i = 0; i < count; i++) {
        printf("Hit %zu: doc_id=%u score=%.4f\n", i + 1, hits[i].ref, hits[i].score);
    }

    sepal_close(vs);
    return 0;
}
```

Compile and run:
```bash
cc search.c $(pkg-config --cflags --libs sepal) -o search
./search
```

---

## 4. Caching & Performance Tuning

### Embedding Cache
Set `CORM_SEPAL_EMBED_CACHE` to avoid re-embedding repeated text across runs:

```bash
export CORM_SEPAL_EMBED_CACHE="$HOME/.cache/sepal-embed.db"
```

### Exact Dimension Truncation
`SEPAL_EXACT_DIM` controls how many dimensions are evaluated during the exact cosine rerank stage (default `256`). Lower values speed up search at a minor accuracy trade-off:

```bash
export SEPAL_EXACT_DIM=128
```

---

## 5. Installation & Model Setup

### Download Embedding Models
Create `~/.models/` and download your chosen model in GGUF format:

```bash
mkdir -p ~/.models

# Option A: Fast & ultra-lightweight (bge-small-en-v1.5, 384 dims, 37 MB)
curl -L -o ~/.models/bge-small-en-v1.5.gguf \
  https://huggingface.co/CompendiumLabs/bge-small-en-v1.5-gguf/resolve/main/bge-small-en-v1.5-q8_0.gguf

# Option B: Higher accuracy (bge-base-en-v1.5, 768 dims, 118 MB)
curl -L -o ~/.models/bge-base-en-v1.5.gguf \
  https://huggingface.co/CompendiumLabs/bge-base-en-v1.5-gguf/resolve/main/bge-base-en-v1.5-q8_0.gguf
```

> **Note on Dimensionality & libsepal**:
> `libsepal` natively handles both 384-dimensional (`bge-small`) and 768-dimensional (`bge-base`) embeddings (up to `SEPAL_VEC_MAX = 2048`). Stage 1 performs sign-bit sketch prefiltering over the full dimension (384 or 768 bits), and Stage 2 performs exact AVX2 cosine reranking over `min(dim, SEPAL_EXACT_DIM)` (default 256 dimensions), giving optimal retrieval accuracy with low CPU and memory footprint.

### Install Packages

#### Debian / Ubuntu / WSL2 (APT)
```bash
sudo mkdir -p /etc/apt/keyrings
wget -qO- https://tty.pt/apt/keyrings/archive-key.gpg | sudo tee /etc/apt/keyrings/ttypt.asc > /dev/null
echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/ttypt.asc] https://tty.pt/deb stable main" | sudo tee /etc/apt/sources.list.d/ttypt.list
sudo apt update && sudo apt install -y axil axil-auren libsepal liblemma libcorm
```

#### macOS (Homebrew)
```bash
brew tap tty-pt/tap https://github.com/tty-pt/tap.git
brew install axil axil-auren libsepal liblemma libcorm
```

#### Arch Linux (pacman)
```bash
sudo pacman-key --add <(wget -qO- https://tty.pt/pacman/key-ttypt.pub)
echo -e "\n[ttypt]\nSigLevel = Required DatabaseOptional\nServer = https://tty.pt/pacman/\$arch" | sudo tee -a /etc/pacman.conf
sudo pacman -Sy ttypt/axil ttypt/axil-auren ttypt/libsepal ttypt/liblemma ttypt/libcorm
```

#### Fedora / RHEL (dnf)
```bash
curl -O https://tty.pt/rpm/ttypt.repo && sudo mv ttypt.repo /etc/yum.repos.d/
sudo rpm --import https://tty.pt/rpm/RPM-GPG-KEY-ttypt
sudo dnf install -y axil axil-auren libsepal liblemma libcorm
```

### Build from Source
```bash
git clone https://github.com/tty-pt/axil.git && cd axil && make && sudo make install && cd ..
git clone https://github.com/tty-pt/libcorm.git && cd libcorm && make && sudo make install && cd ..
git clone https://github.com/tty-pt/liblemma.git && cd liblemma && make && sudo make install && cd ..
git clone https://github.com/tty-pt/libsepal.git && cd libsepal && make && sudo make install && cd ..
git clone https://github.com/tty-pt/axil-auren.git && cd axil-auren && make && sudo make install && cd ..
```

---

## 6. HTTP Service: axil-embed-search Module

For an out-of-the-box REST API and browser UI that coordinates embeddings with `axil-auren` and searches `libsepal` / `liblemma`, use the pure-C `axil-embed-search` module located at `external/axil-embed-search`.

### Build & Run
```bash
./external/axil-embed-search/run.sh
```
Or with a custom port:
```bash
PORT=8090 ./external/axil-embed-search/run.sh
```

### Endpoints
- **`GET /` or `GET /embed-search`**: Interactive web UI form for ingesting prompts and querying results in the browser.
- **`POST /embeddings` (and `/api/embeddings`)**: Ingest prompt/text document, call `axil-auren`, and persist dense vector into `libsepal` and inverted tokens into `liblemma`.
- **`GET /search` (and `/api/search`)**: Query stored documents with semantic similarity (`mode=sepal`), lexical prefix matching (`mode=lemma`), or hybrid ranking (`mode=hybrid`).

For complete documentation, curl cheat sheets, and JSON schemas, see [`external/axil-embed-search/README.md`](../external/axil-embed-search/README.md).

---

## 7. PDF Resume Ingestion & Job Opportunity Search: axil-resume-search Module

For PDF document ingestion and job candidate semantic matching, use the `axil-resume-search` module located at `external/axil-resume-search`.

### Build & Run
```bash
./external/axil-resume-search/run.sh
```

### Endpoints
- **`GET /` or `GET /ui`**: Interactive web UI for uploading resumes, entering job opportunities, and viewing ranked candidate matches with PDF download links.
- **`POST /resume` (and `/api/resume`)**: Upload a PDF resume (`multipart/form-data`, raw binary, or base64 JSON). The module saves the PDF to disk (`uploads/`), invokes `pdftotext` under the hood to extract text, generates embeddings via `axil-auren`, and stores vectors in `libsepal` and metadata in `libcorm`.
- **`GET /candidates` (and `/search`)**: Search and rank candidates by passing a job opportunity description query (`job="Senior C developer..."`). Supports semantic (`mode=sepal`), lexical (`mode=lemma`), and hybrid ranking (`mode=hybrid`).
- **`GET /resume/file?ref=<id>`**: Stream and download the stored PDF resume with `Content-Type: application/pdf`.
- **`GET /candidates/list`**: List all currently ingested candidate records.

For complete documentation and curl examples, see [`external/axil-resume-search/README.md`](../external/axil-resume-search/README.md).
