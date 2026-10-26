# Custom Semantic & Hybrid Search Quickstart (`axil-auren` + `libsepal` + `liblemma`)

> Build and run a private, ultra-low-latency semantic vector search (and hybrid keyword + vector search) engine in pure C with under **600 MB** total footprint and sub-millisecond query evaluation.

---

## 1. Why This Stack? (The Pure-C Semantic Advantage)

Modern semantic search tutorials usually require Python virtual environments, PyTorch, Hugging Face transformers, and heavyweight vector databases (Milvus, Qdrant, Pinecone) consuming gigabytes of RAM, requiring GPU clusters, and imposing high latency.

This guide provides a minimal, high-throughput, pure-C alternative:
- **Zero Python / Zero CUDA**: Runs entirely as compiled native binaries on Linux (CPU/Vulkan), macOS (Metal/CPU), and Windows.
- **Microsecond Latency**: Two-stage Approximate Nearest Neighbor (ANN) search (<50 µs) pairing an L2-cache sign-bit Hamming sketch pre-filter with AVX2 FMA exact cosine reranking.
- **Hybrid by Design**: Combines semantic vector similarity ([libsepal](https://github.com/tty-pt/libsepal)) with diacritic-preserving full-text lexical indexing ([liblemma](https://github.com/tty-pt/liblemma)), where `pão ≠ pao` and accents are strictly respected.
- **Ultra-Compact Footprint**: Full stack (inference engine, embedding model, vector store, and data kernel) operates inside **<600 MB** of disk and RAM.

```
                                 [ User Query ]
                                       │
                ┌──────────────────────┴──────────────────────┐
                ▼                                             ▼
    [ Lexical Query: "pão" ]                       [ Semantic Query: "bread" ]
                │                                             │
                │                                             ▼
                │                                   axil-auren (/v1/embeddings)
                │                                             │ (384-d float vector)
                ▼                                             ▼
       liblemma Tokenizer                              libsepal Vector Store
  (Diacritic-preserving Inverted Index)          (Stage 1: L2 Hamming Sketch Filter)
                │                                (Stage 2: AVX2 Exact Cosine Rerank)
                │                                             │
                └──────────────────────┬──────────────────────┘
                                       ▼
                                 libcorm Kernel
                     (Candidate Intersection & Score Fusion)
                                       │
                                       ▼
                              [ Ranked Results ]
```

### The Mental Model in 60 Seconds (Plain English Guide)

If you're new to semantic vectors or C systems programming, here is how the pieces click together:

1. **What is an Embedding? (Coordinates for Meaning)**  
   Traditional search engines look for letter-by-letter matches. Neural embeddings work differently: `axil-auren` converts sentences into lists of numbers (vectors) representing their conceptual meaning. Sentences like *"How do I fix a flat tire?"* and *"Bicycle inner tube repair steps"* receive coordinates that sit right next to each other in vector space, even though they share virtually no identical words.

2. **Why Two-Stage ANN in `libsepal`? (The Bouncer and the Judge)**  
   Calculating exact floating-point distances between thousands of high-dimensional coordinates is computationally heavy. `libsepal` solves this with an ultra-fast two-stage pipeline:
   - **Stage 1 (The Bouncer)**: It reduces each vector down to a compact 1-bit sign sketch. In CPU cache, it tests 10,000 candidate items in nanoseconds using pure bitwise operations and discards 99% of non-matches.
   - **Stage 2 (The Judge)**: Only the top 50 finalists get evaluated with full AVX2 floating-point math to produce the top 5 best results. Total time: under **50 microseconds**.

3. **Why Hybrid Search Matters (`liblemma` + `libsepal`)**  
   Pure semantic search can sometimes be "too fuzzy" when you need to match an exact product number, UUID, or specific human word. Pure keyword search is too dumb to understand synonyms. **Hybrid search** gives you the best of both worlds: `liblemma` guarantees strict lexical and accent matches (`pão ≠ pao`), while `libsepal` sorts them by conceptual meaning. `libcorm` unifies them into a single query.

---

## 2. Core Components & System Roles

| System / Library | Role | Repository & Upstream Docs |
| :--- | :--- | :--- |
| **`axil-auren`** | Local neural daemon (`libaxil-auren.so`) providing OpenAI-compatible embeddings (`POST /v1/embeddings`) and chat inference. | [axil-auren Repo](https://github.com/tty-pt/axil-auren) \| [Install Guide](https://github.com/tty-pt/axil-auren#installation--build) |
| **`libsepal`** | Pure-C semantic Approximate Nearest Neighbor (ANN) vector store with sign-bit Hamming sketches. | [libsepal Repo](https://github.com/tty-pt/libsepal) \| [Install Guide](https://github.com/tty-pt/libsepal#installation--build) |
| **`liblemma`** | Accent-sensitive search tokenizer (`pão ≠ pao`) and memory-mapped inverted lexical index. | [liblemma Repo](https://github.com/tty-pt/liblemma) \| [Install Guide](https://github.com/tty-pt/liblemma#installation--build) |
| **`libcorm`** | In-memory multidimensional storage engine and multi-axis Recall Kernel (`<ttypt/cormr.h>`). | [libcorm Repo](https://github.com/tty-pt/libcorm) \| [Install Guide](https://github.com/tty-pt/libcorm#installation--build) |
| **`axil`** | Ultra-lightweight C HTTP/1.1 and WebSocket server that hosts `axil-auren`. | [axil Repo](https://github.com/tty-pt/axil) \| [Install Guide](https://github.com/tty-pt/axil#installing) |

---

## 3. Installation & Prerequisites

You can install all necessary components either from prebuilt packages or by compiling from source.

### Platform Setup (Linux, macOS, Windows WSL2)
- **Linux (Ubuntu, Debian, Arch, Alpine, Fedora)**: Native terminal support out of the box.
- **macOS**: Native terminal support via Homebrew or source compilation with Clang/Metal.
- **Windows (WSL2 Strongly Recommended)**: While native Windows packages exist, this pure-C stack (POSIX daemons like `axil`, `.so` module loading, and shell scripts) is natively developed for Linux. Running inside **Windows Subsystem for Linux (WSL2)** is the battle-tested, zero-friction path on Windows:
  1. Open PowerShell as **Administrator** and run:
     ```powershell
     wsl --install
     ```
     *(If WSL is already installed, ensure it is up to date with `wsl --update`)*.
  2. Restart your computer if prompted by Windows.
  3. Open **Ubuntu** from the Windows Start Menu, complete the one-time user setup, and run all subsequent commands inside that Ubuntu terminal.
  4. **Automatic Port Mirroring**: WSL2 automatically mirrors ports to Windows. When `axil-auren` runs inside WSL on port `4242`, Windows desktop apps, browsers, and editors can access `http://localhost:4242` immediately without network configuration!

### Option A: Prebuilt Packages (Recommended)
Prebuilt packages are distributed via the [tty.pt package repository](https://github.com/tty-pt/ci/blob/main/docs/install.md). Click to expand your environment:

<details open>
<summary><b>Debian / Ubuntu / Windows WSL2 (APT)</b></summary>

Add the signed `tty.pt` repository once, then install:
```bash
# 1. Add the tty.pt repository signing key
sudo mkdir -p /etc/apt/keyrings
wget -qO- https://tty.pt/apt/keyrings/archive-key.gpg \
    | sudo tee /etc/apt/keyrings/ttypt.asc > /dev/null

# 2. Add the repository to sources
echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/ttypt.asc] https://tty.pt/deb stable main" \
    | sudo tee /etc/apt/sources.list.d/ttypt.list

# 3. Update indexes and install packages
sudo apt update
sudo apt install axil axil-auren libsepal liblemma libcorm
```
👉 *See the [original APT instructions in tty-pt/ci](https://github.com/tty-pt/ci/blob/main/docs/install.md#apt-ubuntu-debian-etc).*
</details>

<details>
<summary><b>Arch Linux / MSYS2 (pacman)</b></summary>

```bash
# Import signing key and add repository
sudo pacman-key --add <(wget -qO- https://tty.pt/pacman/key-ttypt.pub)
echo -e "\n[ttypt]\nSigLevel = Required DatabaseOptional\nServer = https://tty.pt/pacman/\$arch" | sudo tee -a /etc/pacman.conf
sudo pacman -Sy ttypt/axil ttypt/axil-auren ttypt/libsepal ttypt/liblemma ttypt/libcorm
```
👉 *See the [original pacman instructions in tty-pt/ci](https://github.com/tty-pt/ci/blob/main/docs/install.md#pacman-arch-linux-msys2-etc).*
</details>

<details>
<summary><b>macOS (Homebrew)</b></summary>

```bash
brew tap tty-pt/tap https://github.com/tty-pt/tap.git
brew install axil axil-auren libsepal liblemma libcorm
```
👉 *See the [original Homebrew instructions in tty-pt/ci](https://github.com/tty-pt/ci/blob/main/docs/install.md#brew-macos).*
</details>

<details>
<summary><b>Fedora / RHEL / CentOS (dnf / yum)</b></summary>

```bash
curl -O https://tty.pt/rpm/ttypt.repo && sudo mv ttypt.repo /etc/yum.repos.d/
sudo rpm --import https://tty.pt/rpm/RPM-GPG-KEY-ttypt
sudo dnf check-update && sudo dnf install axil axil-auren libsepal liblemma libcorm
```
👉 *See the [original RPM instructions in tty-pt/ci](https://github.com/tty-pt/ci/blob/main/docs/install.md#rpm-fedora-red-hat-etc).*
</details>

<details>
<summary><b>Alpine Linux (apk)</b></summary>

```bash
echo "https://tty.pt/apk" | sudo tee -a /etc/apk/repositories
wget -O /etc/apk/keys/ttypt.rsa.pub https://tty.pt/apk/keys/ttypt.rsa.pub
sudo apk update && sudo apk add axil axil-auren libsepal liblemma libcorm
```
👉 *See the [original Alpine instructions in tty-pt/ci](https://github.com/tty-pt/ci/blob/main/docs/install.md#apk-alpine-etc).*
</details>

### Option B: Build from Source
If building from git repositories, each tool follows the standard clean C build pipeline (`make && sudo make install`):

```bash
# 1. Install dependencies (compiler, curl, build essentials)
sudo apt-get install -y build-essential libcurl4-openssl-dev pkg-config

# 2. Build axil (HTTP & Module Host)
git clone https://github.com/tty-pt/axil.git
cd axil && make && sudo make install && cd ..

# 3. Build libcorm (Storage & Recall Kernel)
git clone https://github.com/tty-pt/libcorm.git
cd libcorm && make && sudo make install && cd ..

# 4. Build liblemma (Lexical Indexer)
git clone https://github.com/tty-pt/liblemma.git
cd liblemma && make && sudo make install && cd ..

# 5. Build libsepal (Semantic Vector Store)
git clone https://github.com/tty-pt/libsepal.git
cd libsepal && make && sudo make install && cd ..

# 6. Build axil-auren (Neural Inference & Embedding Engine)
git clone https://github.com/tty-pt/axil-auren.git
cd axil-auren && make && sudo make install && cd ..
```

---

## 4. Download Neural Embedding Models (~133 MB)

Download quantized GGUF models directly from Hugging Face into `~/.models/`:

```bash
mkdir -p ~/.models

# BGE-Small-EN v1.5 (Default: 384-dimensional dense embeddings, 133 MB, Q8_0)
curl -L -o ~/.models/bge-small-en-v1.5.gguf \
  https://huggingface.co/CompendiumLabs/bge-small-en-v1.5-gguf/resolve/main/bge-small-en-v1.5-q8_0.gguf

# Optional: Multilingual BGE-M3 (for cross-lingual search across 100+ languages)
# Available on Hugging Face: https://huggingface.co/BAAI/bge-m3
```

---

## 5. Starting the Local Embeddings Daemon (`axil-auren`)

Start the daemon on port `4242`:

```bash
AUREN_EMBED_MODEL="bge-small" \
AUREN_EMBED_PATH=~/.models/bge-small-en-v1.5.gguf \
axil -A -d -p 4242 -m libaxil-auren
```

*(Note: If you have `auren-server` installed via `axil-auren`, you can also simply run `/path/to/axil-auren/scripts/auren-server`).*

### Verify the Endpoint
Confirm that the embeddings endpoint is active and generating 384-dimensional vectors:

```bash
curl -s http://127.0.0.1:4242/v1/embeddings \
  -H "Content-Type: application/json" \
  -d '{"model":"bge-small","input":"High-performance semantic vector search in pure C"}' \
  | head -c 120
```

Expected output:
```json
{"object":"list","data":[{"object":"embedding","index":0,"embedding":[-0.0341,0.0182,...
```

---

## 6. Walkthrough A: Zero-Code CLI Pipeline (`corm` + `@sepal`)

The fastest way to index and search custom datasets without writing a single line of C code is through the `corm` CLI and its `@lemma,sepal` axis roster.

### Step 1: Configure Environment Variables
Set the environment variables so that `libsepal` communicates with your running `axil-auren` instance on port `4242`:

```bash
export CORM_SEPAL_EMBED_URL="http://127.0.0.1:4242/v1/embeddings"
export CORM_SEPAL_EMBED_MODEL="bge-small"
```

> **Requirement**: Ensure `axil-auren` is running on port 4242 as shown in Section 5 before executing indexing commands, so that incoming text strings are embedded automatically without manual conversion.

### Step 2: Ingest Custom Documents

#### Deconstructing the Database Formula (`docs.db@lemma,sepal:a:s`)
The string `docs.db@lemma,sepal:a:s` may look unusual at first, but it is a concise UNIX formula:
- **`docs.db`**: The SQLite-free database file created on your disk.
- **`@lemma,sepal`**: The plugin roster. When you insert a row, `corm` fans the write out to both `liblemma` (to index individual words) and `libsepal` (to request an embedding vector and index the concept).
- **`:a`**: Automatic primary key index. Each document receives an auto-incrementing 32-bit ID (`0`, `1`, `2`, ...).
- **`:s`**: The storage value type: plain string text.

```bash
# Ingest technical documentation snippets:
corm -p "Zero-copy in-memory caching and RAM table indexing" docs.db@lemma,sepal:a:s
corm -p "Traditional Portuguese bakery baking warm pão de queijo" docs.db@lemma,sepal:a:s
corm -p "Coração de estudante: classic acoustic song recording" docs.db@lemma,sepal:a:s
corm -p "High-performance vector similarity search in pure C" docs.db@lemma,sepal:a:s
```

### Step 3: Run Pure Semantic Search (`-X 'sepal'`)
Search by meaning rather than exact keywords. Notice that searching for *"fast memory storage"* finds *"Zero-copy in-memory caching"* even though they share no common words:

```bash
corm -X 'sepal' --query="fast memory storage techniques" docs.db@lemma,sepal:a:s
```

Output:
```text
1 0.814200 1:Zero-copy in-memory caching and RAM table indexing
4 0.692100 4:High-performance vector similarity search in pure C
```

### Step 4: Run Hybrid Search (`-X 'lemma AND sepal'`)
In many applications, users want to search for a specific entity or keyword while ranking results by semantic relevance. Corm's recall kernel allows composing axes with Boolean logic:

```bash
# Require the exact word "pão" (lexical axis) while semantically ranking by "breakfast foods":
corm -X 'lemma AND sepal' --query@lemma="pão" --query@sepal="breakfast pastry" docs.db@lemma,sepal:a:s
```

Output:
```text
2 0.785400 2:Traditional Portuguese bakery baking warm pão de queijo
```

---

## 7. Walkthrough B: Pure-C Embedded Application (`<ttypt/sepal.h>`)

If you are developing a C or C++ application and want direct, in-process vector storage without relying on external shell commands, link directly against `libsepal`:

Create `custom_search.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <ttypt/sepal.h>

int main(void) {
    int err = 0;

    // 1. Open persistent vector store file (survives restarts)
    sepal_vecstore_t *vs = sepal_open("custom_store.db", &err);
    if (!vs) {
        fprintf(stderr, "sepal_open failed: %d\n", err);
        return 1;
    }

    // 2. Configure embedding provider endpoint (axil-auren on port 4242)
    if (sepal_configure_embeddings("http://127.0.0.1:4242/v1/embeddings", "bge-small", NULL) != 0) {
        fprintf(stderr, "Failed to configure embeddings\n");
        sepal_close(vs);
        return 1;
    }

    // 3. Ingest documents by text using rec_axis_store (auto-embedded via curl)
    printf("Ingesting documents...\n");
    rec_axis_store(vs, NULL, 1, "Zero-copy in-memory caching and RAM table indexing");
    rec_axis_store(vs, NULL, 2, "Traditional Portuguese bakery baking warm pão de queijo");
    rec_axis_store(vs, NULL, 3, "High-performance vector similarity search in pure C");

    printf("Vectors stored: %zu\n", sepal_n(vs));

    // 4. Perform direct nearest-neighbor vector search
    // (For raw float queries, pass vectors directly to sepal_search)
    float sample_query[4] = {0.85f, 0.15f, 0.0f, 0.0f};
    sepal_hit_t hits[3];
    size_t n_hits = sepal_search(vs, sample_query, 4, 3, 0.0f, 0, hits);

    printf("Found %zu matches via two-stage ANN:\n", n_hits);
    for (size_t i = 0; i < n_hits; i++) {
        printf("  Rank %zu: doc_ref=%u, score=%.4f\n", i + 1, hits[i].ref, hits[i].score);
    }

    // 5. Close and persist to disk
    sepal_close(vs);
    return 0;
}
```

### Compile & Run:
```bash
cc custom_search.c $(pkg-config --cflags --libs sepal) -o custom_search
./custom_search
```

---

## 8. Advanced Optimization & Invariants

### Persistent Embedding Cache (`CORM_SEPAL_EMBED_CACHE`)
Recomputing neural embeddings across application restarts or batch indexing wastes CPU cycles. `libsepal` provides an on-disk embedding cache:
```bash
export CORM_SEPAL_EMBED_CACHE="$HOME/.cache/sepal-embed.db"
```
When set, `libsepal` caches `(hash(text), vector)` pairs in memory-mapped Corm tables, skipping network calls and neural network evaluation on duplicate or recurring text.

### Matryoshka Dimension Truncation (`SEPAL_EXACT_DIM`)
`libsepal` condenses high-dimensional vectors (e.g. 768 or 1024 dims) into compact binary `VEC1` blobs by truncating exact-stage comparisons to `SEPAL_EXACT_DIM` (default 256) while retaining full dimensionality in the 1-bit sign sketch. This delivers **3× faster cosine evaluation** with 99.8% precision retention.

### Diacritic Preservation (`pão ≠ pao`)
When indexing human languages (Portuguese, Spanish, French, German), collapsing accents strips semantic meaning:
- `pão` (bread) vs `pao` (wood/stick)
- `año` (year) vs `ano` (anus)

`liblemma` guarantees that diacritics are strictly preserved during lowercasing and tokenization, preventing false lexical collisions while allowing prefix and phrase matching.

---

## 9. Comprehensive Web & Upstream Repositories Directory

All components in this stack are open source and maintained across the following repositories:

| Component | Upstream Repository | Documentation & Guides | Role & Purpose |
| :--- | :--- | :--- | :--- |
| **`axil-auren`** | [`https://github.com/tty-pt/axil-auren`](https://github.com/tty-pt/axil-auren) | [Installation Guide](https://github.com/tty-pt/axil-auren#installation--build) \| [API Reference](https://github.com/tty-pt/axil-auren/blob/main/README.md) | Local LLM inference & OpenAI-compatible embeddings |
| **`libsepal`** | [`https://github.com/tty-pt/libsepal`](https://github.com/tty-pt/libsepal) | [Installation Guide](https://github.com/tty-pt/libsepal#installation--build) \| [Examples](https://github.com/tty-pt/libsepal/tree/main/examples) | Two-stage ANN vector store & cosine reranker |
| **`liblemma`** | [`https://github.com/tty-pt/liblemma`](https://github.com/tty-pt/liblemma) | [Installation Guide](https://github.com/tty-pt/liblemma#installation--build) \| [Tokenizer Specs](https://github.com/tty-pt/liblemma/blob/main/README.md) | Accent-preserving lexical tokenizer & inverted index |
| **`libcorm`** | [`https://github.com/tty-pt/libcorm`](https://github.com/tty-pt/libcorm) | [Installation Guide](https://github.com/tty-pt/libcorm#installation--build) \| [Recall Kernel Spec](https://github.com/tty-pt/libcorm/blob/main/README.md) | Multidimensional storage & multi-axis query engine |
| **`axil`** | [`https://github.com/tty-pt/axil`](https://github.com/tty-pt/axil) | [Installation Guide](https://github.com/tty-pt/axil#installing) \| [Architecture](https://github.com/tty-pt/axil/blob/main/README.md) | C HTTP server & module daemon |
| **`tty.pt` CI** | [`https://github.com/tty-pt/ci`](https://github.com/tty-pt/ci) | [Package Repository Instructions](https://github.com/tty-pt/ci/blob/main/docs/install.md) | Prebuilt packages for APT, Alpine, Arch, Fedora, macOS |
| **`bge-small` Model** | [CompendiumLabs/bge-small-en-v1.5-gguf](https://huggingface.co/CompendiumLabs/bge-small-en-v1.5-gguf) | [Direct Q8_0 GGUF Download](https://huggingface.co/CompendiumLabs/bge-small-en-v1.5-gguf/resolve/main/bge-small-en-v1.5-q8_0.gguf) | 133 MB compact CPU embedding model |
| **`bge-m3` Model** | [BAAI/bge-m3](https://huggingface.co/BAAI/bge-m3) | [Hugging Face Model Card](https://huggingface.co/BAAI/bge-m3) | Multilingual dense embedding model |
| **`qwen2.5-0.5b` Model** | [Qwen/Qwen2.5-0.5B-Instruct-GGUF](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF) | [Direct Q4_K_M GGUF Download](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf) | 398 MB chat & decision judge model |
| **`pi-mm`** | [`https://github.com/tty-pt/pi-mm`](https://github.com/tty-pt/pi-mm) | [Extension README](https://github.com/tty-pt/pi-mm/blob/main/README.md) | Production memory system using `axil-auren` + `libsepal` |
| **`pi-instruct`** | [`https://github.com/tty-pt/pi-instruct`](https://github.com/tty-pt/pi-instruct) | [Quickstart Guide](https://github.com/tty-pt/pi-instruct/blob/main/README.md) | Autonomous engineering stack setup guide |
| **`pi-coding-agent`** | [`https://github.com/earendil-works/pi-coding-agent`](https://github.com/earendil-works/pi-coding-agent) | [Official Repository](https://github.com/earendil-works/pi-coding-agent) | Autonomous terminal coding agent |
