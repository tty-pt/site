#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SOFTWARE_DIR="${REPO_ROOT}/var/software"

mkdir -p "$SOFTWARE_DIR"

create_software() {
  local id="$1"
  local title="$2"
  local repo="$3"
  local summary="$4"
  local version="$5"
  local license="$6"
  local category="$7"
  local owner="$8"
  local readme="$9"

  local dir="$SOFTWARE_DIR/$id"
  rm -rf "$dir"
  mkdir -p "$dir"
  printf "%s" "$id" > "$dir/id"
  printf "%s" "$title" > "$dir/title"
  printf "%s" "$repo" > "$dir/repo_url"
  printf "%s" "$summary" > "$dir/summary"
  printf "%s" "$version" > "$dir/version"
  printf "%s" "$license" > "$dir/license"
  printf "%s" "$category" > "$dir/category"
  printf "%s" "$owner" > "$dir/owner"
  printf "%s\n" "$readme" > "$dir/README.md"
  touch "$dir/comments.txt"
}

# 1. libcorm
create_software "corm" "libcorm" "https://github.com/tty-pt/corm" \
  "Zero-copy compact typed C records, serialized disk storage & hierarchical B-tree indexing" \
  "2.0.0" "BSD-2-Clause" "Core Storage" "tty-pt" \
  "# libcorm

**libcorm** is a zero-copy, compact typed C object storage engine designed for high-throughput, low-memory embedded applications.

## Why libcorm?
Traditional web applications rely on heavyweight external databases (PostgreSQL, SQLite, Redis) that introduce serialization overhead, context switching, and network latency. **libcorm** eliminates database overhead by embedding typed record schemas directly into C process memory.

## Architectural Pillars
- **Zero-Copy Record Schemas**: 24-byte record descriptors map struct fields to binary memory without intermediate JSON or SQL conversion.
- **Hierarchical B-Tree & Hash Indexing**: Fast sub-microsecond point lookups and range scans.
- **Multi-Axis Record Trees**: Allows indexing data along multiple arbitrary axes (e.g. key, timestamp, tag, relationship).
- **Persistent Disk Serialization**: Clean file-backed persistence with crash recovery and append logs.

## Code Example

\`\`\`c
#include <ttypt/corm.h>

typedef struct {
    uint32_t id;
    char name[64];
    uint64_t score;
} player_record_t;

int main(void) {
    /* Open an axis indexed by string key, storing 32-bit record pointers */
    unsigned hd = corm_open(\"var/players\", NULL, CM_STR, CM_U32, 0x1FF, 0);

    /* Store and retrieve records in constant time */
    uint32_t rec_id = 101;
    corm_put(hd, \"player:alice\", &rec_id);

    uint32_t *found = (uint32_t *)corm_get(hd, \"player:alice\");
    if (found) {
        printf(\"Found player ID: %u\\n\", *found);
    }

    corm_close(hd);
    return 0;
}
\`\`\`

## Integration
Serves as the foundation for **libhyle**, **axil-nd**, and the entire site metadata layer. Check the [GitHub Repository](https://github.com/tty-pt/corm) for benchmarks and API documentation."

# 2. axil
create_software "axil" "axil" "https://github.com/tty-pt/axil" \
  "Ultra-low latency event-driven HTTP/1.1 and WebSocket server library in pure C" \
  "1.6.0" "BSD-2-Clause" "Web Server" "tty-pt" \
  "# axil

**axil** is an ultra-low latency, event-driven HTTP/1.1 and WebSocket server engine written entirely in ANSI C.

## Core Capabilities
- **Non-Blocking Epoll / Kqueue Reactor**: Single-threaded and multi-threaded event loops capable of serving 100,000+ requests/sec with minimal CPU utilization.
- **Zero-Copy HTTP Framing**: Parses incoming request lines and headers in-place without heap allocations.
- **WebSocket Protocol Engine**: Full support for RFC 6455 framing, automatic handshake upgrades, ping/pong heartbeats, and raw binary/text channel multiplexing.
- **Dynamic Module Architecture**: Integrates directly with **libxylem** to dynamically load and unload request handlers at runtime without restarting the daemon.

## Architecture

\`\`\`
[ Client Browser / TCP ]
           |
           v
  [ axil Event Reactor ] (epoll / kqueue non-blocking I/O)
           |
     +-----+-----+
     |           |
  [ HTTP/1.1 ] [ WebSocket Channels ]
     |           |
     v           v
  [ libxylem Dynamic Module Dispatcher ]
     |           |           |
     v           v           v
  [ poem.so ] [ song.so ] [ software.so ]
\`\`\`

## Benchmark Highlights
- **Request Latency**: Sub-100 microsecond response times for static assets and SSR routes.
- **Memory Footprint**: Less than 8 MB RSS under heavy concurrency.

Visit the [axil GitHub repository](https://github.com/tty-pt/axil) to download source and modules."

# 3. axil-auren
create_software "axil-auren" "axil-auren" "https://github.com/tty-pt/axil-auren" \
  "Local LLM inference & OpenAI-compatible streaming runtime running on Axil" \
  "1.3.0" "BSD-2-Clause" "AI Engine" "tty-pt" \
  "# axil-auren

**axil-auren** brings local Large Language Model (LLM) inference directly into the C systems runtime. Powered by **Axil** and **llama.cpp**, it eliminates the heavy Python dependency stack (PyTorch, Hugging Face, vLLM) in favor of a lean, high-performance binary.

## Features
- **OpenAI Compatible Streaming**: Implements \`POST /v1/chat/completions\` with streaming Server-Sent Events (SSE).
- **Dense Vector Embeddings**: Exposes \`POST /v1/embeddings\` for semantic search with sub-millisecond vector extraction.
- **GGUF Direct Execution**: Native support for modern quantized architectures: Qwen2.5, Llama 3, Phi-3, and Mixture-of-Experts (OLMoE).
- **Zero Configuration Daemon**: Automatically provisions hardware backends (AVX2, Metal, CUDA) and manages model memory.

## Usage Example

\`\`\`bash
# Request streaming chat completion from local axil-auren on port 4242
curl http://127.0.0.1:4242/v1/chat/completions \\
  -H \"Content-Type: application/json\" \\
  -d '{
    \"model\": \"qwen2.5-0.5b-instruct.gguf\",
    \"messages\": [{\"role\": \"user\", \"content\": \"Explain zero-copy in C\"}],
    \"stream\": true
  }'
\`\`\`

## Ecosystem Integration
\`axil-auren\` provides real-time vector embeddings to **libsepal** and feeds semantic search across our web services. Learn more on [GitHub](https://github.com/tty-pt/axil-auren)."

# 4. libsepal
create_software "sepal" "libsepal" "https://github.com/tty-pt/sepal" \
  "Dense vector embeddings, SIMD-accelerated cosine similarity & vector indexing in pure C" \
  "1.2.0" "BSD-2-Clause" "Vector Search" "tty-pt" \
  "# libsepal

**libsepal** is a lightweight, SIMD-optimized dense vector storage and similarity search library written in pure C.

## Performance & Optimization
- **SIMD Hardware Acceleration**: Employs handwritten AVX2 and ARM NEON intrinsics for vector dot-product and cosine distance computations.
- **Cache-Line Alignment**: Embeddings are 64-byte aligned in memory to saturate CPU L1/L2 data cache throughput.
- **Zero-Allocation Queries**: Top-K search traverses vectors using bounded stack buffers without allocating memory per query.

## Integration with axil-auren
When \`axil-auren\` computes dense vectors from text, \`libsepal\` indexes them directly in disk-backed arrays. Together with \`liblemma\`, it delivers hybrid search combining semantic intent with exact keyword matching.

Explore the source and benchmarks at [libsepal on GitHub](https://github.com/tty-pt/sepal)."

# 5. liblemma
create_software "lemma" "liblemma" "https://github.com/tty-pt/lemma" \
  "Pure-C fast lexical search, BM25 ranking, tokenization & accent folding" \
  "1.1.0" "BSD-2-Clause" "Search Engine" "tty-pt" \
  "# liblemma

**liblemma** is a high-speed lexical search and text indexing engine implemented in pure ANSI C.

## Highlights
- **BM25 Probabilistic Ranking**: Computes term frequency and inverse document frequency (TF-IDF) metrics natively.
- **Accent & Diacritic Normalization**: Built-in accent folding (\`axil_slugify\`) ensuring Portuguese and international searches (e.g. *coração* vs *coracao*) return accurate matches.
- **Prefix & Quoted Phrase Matching**: Supports contiguous multi-token phrases and prefix auto-completion.
- **Memory Efficient Inverted Index**: Compresses posting lists to minimize disk footprint.

## Hybrid Search (Sepal + Lemma)
By combining **libsepal** (dense semantic vectors) and **liblemma** (sparse lexical tokens) via Reciprocal Rank Fusion (RRF), our services achieve best-in-class retrieval precision.

Visit [liblemma on GitHub](https://github.com/tty-pt/lemma)."

# 6. libxylem
create_software "xylem" "libxylem" "https://github.com/tty-pt/xylem" \
  "Dynamic C module loader, zero-cost XY hooks & runtime dependency DAG" \
  "1.5.0" "BSD-2-Clause" "Module System" "tty-pt" \
  "# libxylem

**libxylem** (XY) is the dynamic module and extensibility engine that powers our pure-C architecture.

## The XY Boundary Contract
To maintain modularity and prevent spaghetti dependencies, all cross-module interactions must adhere to the **Two-Header Rule**:
1. \`M-types.h\`: Declares shared data structures and function prototypes.
2. \`M.h\`: Declares caller-facing \`XY_DECL\` hooks.
3. The implementing module includes \`M-types.h\` and implements hooks via \`XY_IMPL\`. Implementers *never* include caller headers.

## Features
- **Zero-Cost Function Hook Dispatch**: Function pointers resolve once at module load time; invocations compile directly to indirect branch instructions.
- **Explicit Runtime DAG**: Dependencies are declared via \`xy_load()\` in \`xy_install()\`, preventing circular dependencies.
- **Hot Reloading**: Modules can be dynamically unloaded, recompiled, and linked without dropping open network sockets.

Check out the full specification at [libxylem on GitHub](https://github.com/tty-pt/xylem)."

# 7. libbud & libhyle
create_software "bud-hyle" "bud & hyle" "https://github.com/tty-pt/site" \
  "The isomorphic framework pair: pure-C data schemas, SSR rendering & browser WASM hydration" \
  "2.1.0" "BSD-2-Clause" "UI Framework" "tty-pt" \
  "# bud & hyle (The Isomorphic Framework Pair)

**libbud** and **libhyle** form our dual-layer, pure-C web framework. They deliver modern reactive user experiences without a single line of React, Dioxus, or Rust.

## Separation of Concerns
- **libhyle**: Owns canonical data schemas (\`hyle_schema_desc_t\`), query execution, and storage drivers with **zero DOM concepts**.
- **libbud**: Pure 5-field UI binder (\`bud_field_desc_t\`) and reactive node builder with **zero database concepts**.
- **libhyle-bud**: The small, explicit bridge unifying data schemas with UI forms, tables, and omni-search pickers.

## Isomorphic Dual-Compilation
All UX code in \`mods/*/ux/\` compiles **twice**:
1. **Native Shared Object (\`.so\`)**: Server-Side Rendering (SSR) generates clean, accessible HTML before sending to the client.
2. **WebAssembly Reactor (\`.wasm\`)**: The exact same C source code compiles via Clang/WASI into \`htdocs/*.wasm\`, hydrating the DOM in the browser for instant client-side interactivity!

## Zero Site-Specific JavaScript
Browser JavaScript (\`htdocs/*.js\`) is strictly generic infrastructure (\`bud-client.js\`, \`bud-hydrate.js\`). No custom application logic, URLs, or business rules exist in JS.

Read the [documentation and architecture guides](https://github.com/tty-pt/site)."

# 8. axil-nd
create_software "axil-nd" "axil-nd" "https://github.com/tty-pt/axil-nd" \
  "NeverDark MUCK virtual world simulation engine ported to pure-C Axil" \
  "0.9.0" "BSD-2-Clause" "Virtual World" "tty-pt" \
  "# axil-nd

**axil-nd** is the NeverDark multi-user virtual world and MUCK simulation engine ported to **Axil**.

## Features
- **High-Concurrency Spatial Engine**: Manages 2D coordinate spaces, rooms, entities, mobs, plants, and celestial bodies.
- **Binary Command Protocol (BCP)**: Compact multiplexed protocol running over WebSockets for real-time player actions, vitals, and inventory.
- **Dual Display Drivers**:
  - **ASCII Stage**: Renders high-fidelity terminal character maps with ANSI colors.
  - **Lamina 2D Canvas**: WebGL and canvas-based 2D map tiles with biomes and entity markers.
- **Persistent Universe**: Compact universe storage in \`var/nd/universe.db\`.

View the port progress and architecture at [axil-nd on GitHub](https://github.com/tty-pt/axil-nd)."

# 9. pi-mm
create_software "pi-mm" "pi-mm" "https://github.com/tty-pt/pi-mm" \
  "Memory Mipmaps extension for Pi Coding Agent — hierarchical recall & zero-config local models" \
  "0.4.1" "BSD-2-Clause" "Agent Extension" "tty-pt" \
  "# pi-mm

**pi-mm** is the Memory Mipmaps extension for the **Pi Coding Agent**, providing persistent hierarchical task memory, standing directives reinforcement, and turnkey local inference.

## Key Capabilities
- **Hierarchical Memory Mipmaps**:
  - \`L0\`: High-level executive overview
  - \`L1\`: Operational timeline and milestone summary
  - \`L2\`: Concrete approaches, reasons, and git diffs
  - \`L3\`: Multi-resolution evidence tree and verification receipts
- **Permanent Standing Directives (\`/mm rule\`)**: Rules survive context resets and compactions by being periodically re-injected at turn milestones.
- **Proactive C Engine Tracking (\`/mm doctor\`)**: Automatically validates installed versions of \`libcorm\`, \`libsepal\`, \`libjoint\`, \`liblemma\`, \`axil\`, and \`axil-auren\`.
- **Zero-Config Inference Daemon**: Automatically boots \`axil-auren\` on port 4242 in the background and provisions verified models.

Get the package or extension bundle on [GitHub](https://github.com/tty-pt/pi-mm)."

# 10. pi-quest
create_software "pi-quest" "pi-quest" "https://github.com/tty-pt/pi-quest" \
  "Autonomous quest journal, immutable task ledger & specification-mapped lifecycle driver" \
  "2.0.0" "BSD-2-Clause" "Agent System" "tty-pt" \
  "# pi-quest

**pi-quest** is the autonomous quest journal and task lifecycle management system for coding agents.

## Core Philosophy
Every complex engineering task must maintain its source of truth on disk (\`.pi/quest/future/<qid>.md\`). This prevents context reset amnesia and ensures decisions and evidence survive session boundaries.

## Architecture
- **State Machine**: \`drafting\` &rarr; \`implementing\` &rarr; \`validating\` &rarr; \`completed\` / \`archived\`.
- **Maturity Bar**: Drafts must meet strict requirements and evidence thresholds before entering execution.
- **Spec-Mapped Traceability**: Automatically links requirements directly to code tags and verification tests.
- **Adversarial Verification Gate**: Validates that all changes compile cleanly, pass unit/integration tests, and introduce zero architectural boundary violations.

Documentation and releases on [GitHub](https://github.com/tty-pt/pi-quest)."

echo "Software catalog seeded successfully with 10 stack showcase articles in $SOFTWARE_DIR"
