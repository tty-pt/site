# axil-resume-search

A high-performance pure-C [Axil](https://github.com/tty-pt/axil) service module for PDF resume ingestion, automatic text extraction via `pdftotext`, physical file storage, and semantic candidate-to-job matching powered by local neural embeddings (`axil-auren`), vector search (`libsepal`), and lexical indexing (`liblemma`).

---

## ⚡ 60-Second Quickstart

### 1. Launch the Server
```bash
./external/axil-resume-search/run.sh
```
*(Starts on `http://localhost:8092`. Auto-builds the `.so` module if needed).*

### 2. Upload a Resume
```bash
curl -s -X POST http://localhost:8092/resume \
  -F "file=@external/axil-resume-search/test_resumes/paulo_lead_frontend_systems_architect.pdf;type=application/pdf" | jq
```

### 3. Match Candidates to a Job Opportunity
```bash
curl -s "http://localhost:8092/candidates?job=Lead+Frontend+Engineer+with+WASM+and+C+experience&mode=hybrid" | jq .candidates[0]
```

### 4. Open the Web Dashboard
Open **`http://localhost:8092/`** in your browser to upload PDFs and search candidates visually.

---

## 🌐 Interactive Web Dashboard

When running `run.sh`, navigate to **`http://localhost:8092/`** in any web browser:

1. **📤 Upload Resume (PDF)**:
   - Click the file picker and select any PDF (e.g. from `test_resumes/` or your own PDF).
   - Enter an optional Candidate Name (if omitted, the system extracts the name from the PDF text automatically).
   - Click **Upload & Ingest Resume**. The engine extracts text via `pdftotext`, saves the physical file in `uploads/`, computes dense vectors via `axil-auren`, and displays success stats.
2. **🎯 Match Job Opportunity**:
   - Paste a natural language job description or requirements list into the textarea.
   - Choose a matching mode: **Hybrid** (recommended), **Semantic (Sepal)**, or **Lexical (Lemma)**.
   - Click **Find Matching Candidates**.
3. **🏆 Matched Candidates**:
   - Displays ranked candidate cards with similarity scores (e.g. `Score: 78.5%`), resume excerpt snippets, and direct buttons to **👁 View PDF** or **⬇ Download PDF**.

---

## 📡 REST API Reference

| Method | Endpoint | Description | Content-Type | Parameters / Body |
|---|---|---|---|---|
| `POST` | `/resume` (or `/api/resume`, `/upload`) | Ingest PDF resume, extract text, embed & store | `multipart/form-data`, `application/pdf`, `application/json` | File binary or `file=@...`, optional `candidate`, `filename` |
| `GET` | `/candidates` (or `/search`, `/api/candidates`) | Semantically match candidates to job opportunity | `application/json` | `?job=...` (or `?q=`), `?mode=hybrid\|sepal\|lemma`, `?k=10` |
| `GET` | `/resume/file` | Stream / download original stored PDF file | `application/pdf` | `?ref=<id>` or `?path=<filepath>` |
| `GET` | `/candidates/list` (or `/resumes`) | List all currently ingested candidate records | `application/json` | None |
| `GET` | `/` (or `/ui`) | Interactive browser dashboard | `text/html` | None |

---

## 📥 Detailed API Endpoints & `curl` Usage

### 1. Ingest a PDF Resume (`POST /resume`)

Supports three upload formats:

#### A. Standard Multipart Form Upload (`multipart/form-data`)
```bash
curl -s -X POST http://localhost:8092/resume \
  -F "candidate=Alice Smith - Systems Engineer" \
  -F "file=@alice_resume.pdf;type=application/pdf"
```

#### B. Direct Binary Stream (`application/pdf`)
```bash
curl -s -X POST "http://localhost:8092/resume?candidate=Bob+Jones&filename=bob_resume.pdf" \
  -H "Content-Type: application/pdf" \
  --data-binary "@bob_resume.pdf"
```

#### C. Base64 JSON Payload (`application/json`)
```bash
B64=$(base64 -w 0 resume.pdf)
curl -s -X POST http://localhost:8092/resume \
  -H "Content-Type: application/json" \
  -d "{\"candidate\":\"Carol White\",\"filename\":\"carol.pdf\",\"pdf_base64\":\"$B64\"}"
```

#### Successful JSON Response:
```json
{
  "ok": true,
  "ref": 1,
  "candidate": "Paulo Quirino",
  "file_name": "paulo_lead_frontend_systems_architect.pdf",
  "file_path": "uploads/resume_1_paulo_lead_frontend_systems_architect.pdf",
  "file_size": 9746,
  "text_len": 4120,
  "dim": 384,
  "snippet": "Paulo Quirino - Lead Frontend Engineer & Systems Architect...",
  "file_url": "/resume/file?ref=1"
}
```

---

### 2. Search Candidates with a Job Opportunity (`GET /candidates`)

```bash
curl -s "http://localhost:8092/candidates?job=Senior+C+systems+programmer+with+Linux+sockets+and+networking&mode=hybrid" | jq
```

#### Query Parameters:
- `job` (or `q` / `opportunity`): Natural language job posting, responsibilities, or skills.
- `mode`: Search algorithm:
  - `hybrid` *(default / recommended)*: Vector cosine similarity + keyword match boost (+0.15).
  - `sepal`: Pure dense vector cosine similarity.
  - `lemma`: Strict lexical keyword matching.
- `k` (or `limit`): Maximum candidate results to return (default: 10, max: 100).
- `min_sim`: Minimum similarity score threshold (default: 0.0).

#### Example Response:
```json
{
  "ok": true,
  "job": "Senior C systems programmer with Linux sockets and networking",
  "mode": "hybrid",
  "count": 1,
  "candidates": [
    {
      "ref": 1,
      "score": 0.8851,
      "candidate": "Alice Smith - Systems Engineer",
      "file_name": "alice_systems_engineer.pdf",
      "file_path": "uploads/resume_1_alice_systems_engineer.pdf",
      "file_size": 24510,
      "snippet": "Alice Smith - Senior Systems Engineer Summary: 10 years experience developing high-performance Linux network daemons in pure C...",
      "file_url": "/resume/file?ref=1",
      "lexical_match": true
    }
  ]
}
```

---

### 3. Download or View Stored PDF (`GET /resume/file?ref=<id>`)

Streams the physical PDF directly with `Content-Type: application/pdf`:
```bash
curl -O -J "http://localhost:8092/resume/file?ref=1"
```

---

### 4. List All Candidates (`GET /candidates/list`)

Returns all currently indexed candidate records:
```bash
curl -s http://localhost:8092/candidates/list | jq
```

---

## 📋 Example Job Opportunities for Testing (Copy & Paste)

These sample job postings are tuned to test semantic matching against the candidate resumes in `test_resumes/`. Paste any of them into the browser dashboard at `http://localhost:8092/` or use with `curl`:

### 1. Lead Systems Frontend Architect
> *"We are seeking a Lead Frontend Engineer and Systems Architect who bridges modern reactive web ergonomics (React 18, TypeScript, Valtio, Zod) with low-level mechanical sympathy in pure C, WebAssembly (WASM), and POSIX event loops. Experience in building real-time robotics telemetry tables, 3D interactive scene manipulation (BabylonJS), autonomous mobile robot interfaces, and datatype-driven component systems is required."*
- **Expected Match**: **Paulo Quirino** (`paulo_lead_frontend_systems_architect.pdf`)
- **Key Triggers**: React, TypeScript, WASM, C99, BabylonJS, Robotics Telemetry.

### 2. Senior Distributed Storage Systems Engineer
> *"High-throughput database infrastructure company is hiring a Senior Distributed Systems Engineer. You will design and implement a fault-tolerant key-value storage engine in asynchronous Rust with Tokio. Deep expertise in the Raft consensus algorithm, LSM-tree internals (SSTables, MemTables, Bloom filters), write-ahead logging (WAL), and zero-copy Linux asynchronous I/O using io_uring is required."*
- **Expected Match**: **Elena Rostova** (`elena_rust_distributed_storage_engineer.pdf`)
- **Key Triggers**: Rust, Tokio, Raft consensus, LSM-tree, RocksDB, io_uring, WAL.

### 3. Staff ML Infrastructure Engineer
> *"Leading AI research laboratory is looking for a Staff Machine Learning Infrastructure Engineer to scale our large language model inference clusters. Candidates must have extensive experience with PyTorch, CUDA kernel optimization, vLLM PagedAttention, TensorRT-LLM, Triton Inference Server, continuous batching, and high-throughput dense vector embedding search architectures."*
- **Expected Match**: **Marcus Vance** (`marcus_ml_nlp_infra_engineer.pdf`)
- **Key Triggers**: PyTorch, CUDA, vLLM, TensorRT-LLM, Triton, Dense vector embeddings.

### 4. Lead Product & Design Systems Designer
> *"We are looking for a Lead Product Designer to own and govern our company-wide design system across web and mobile applications. The role requires deep mastery of Figma variables and design tokens, atomic design component libraries, visual typography hierarchies, user journey research, and ensuring strict WCAG 2.2 AAA accessibility compliance."*
- **Expected Match**: **Sophia Lindqvist** (`sophia_lead_product_designer.pdf`)
- **Key Triggers**: Figma, Design tokens, Design systems, Typography, WCAG AAA.

### 5. Principal Site Reliability Engineer
> *"Global fintech infrastructure provider seeks a Principal Site Reliability Engineer (SRE). Must have extensive hands-on experience operating mission-critical multi-region Kubernetes clusters, writing Terraform infrastructure as code, deploying eBPF networking with Cilium, managing Prometheus/Grafana observability, Linux kernel sysctl tuning, and automated chaos engineering."*
- **Expected Match**: **David Chen** (`david_devops_site_reliability_engineer.pdf`)
- **Key Triggers**: Kubernetes, eBPF, Cilium, Terraform, Prometheus, Linux kernel tuning.

### 6. Senior Embedded Systems & IoT Firmware Engineer
> *"Industrial IoT manufacturer is hiring an Embedded Systems and Firmware Engineer for battery-powered sensor nodes. Requirements include bare-metal C programming on ARM Cortex-M and ESP32 microcontrollers, FreeRTOS task synchronization, low-level hardware peripheral buses (I2C, SPI, UART, CAN), Bluetooth Low Energy (BLE 5.2) stacks, and ultra-low-power sleep optimization."*
- **Expected Match**: **Clara Mendes** (`clara_embedded_firmware_iot_engineer.pdf`)
- **Key Triggers**: Bare-metal C, FreeRTOS, ARM Cortex-M, ESP32, SPI/I2C/UART, BLE.

---

### Quick One-Liner Search Queries

- **Systems & UI**: `"React and TypeScript developer with C and WebAssembly low level experience"` $\rightarrow$ matches **Paulo Quirino**
- **Rust Database**: `"Rust engineer experienced in Raft consensus and storage engines"` $\rightarrow$ matches **Elena Rostova**
- **Deep Learning**: `"ML engineer scaling PyTorch model inference on GPUs with CUDA"` $\rightarrow$ matches **Marcus Vance**
- **UI/UX Design**: `"Lead product designer with Figma design tokens and design systems"` $\rightarrow$ matches **Sophia Lindqvist**
- **SRE & Cloud**: `"Kubernetes platform engineer with eBPF Cilium and Terraform"` $\rightarrow$ matches **David Chen**
- **Embedded C**: `"Bare-metal C and FreeRTOS firmware engineer for ARM Cortex-M"` $\rightarrow$ matches **Clara Mendes**

---

## ⚙️ Configuration & Environment Variables

All settings can be overridden via shell environment variables:

| Environment Variable | Default Value | Description |
|---|---|---|
| `PORT` | `8092` | HTTP listen port for the server |
| `AUREN_EMBED_URL` | `http://127.0.0.1:4242/v1/embeddings` | URL of the `axil-auren` neural embedding endpoint |
| `AUREN_EMBED_MODEL` | `bge-small` | Embedding model name (`bge-small` 384-d or `bge-base` 768-d) |
| `RESUME_UPLOAD_DIR` | `uploads` | Directory on disk where physical PDF files are saved |
| `RESUME_SEARCH_DB` | `NULL` (in-memory) | Optional persistent file path for Sepal vector store |

---

## 📁 Directory Structure & File Map

```text
external/axil-resume-search/
├── run.sh                     # Turnkey launcher script (builds & starts on port 8092)
├── test.sh                    # 10-test unit and API test suite
├── eval_matches.sh            # Automated 6-profile semantic matching evaluation harness
├── Makefile                   # Build rules (targets: all, test, eval, clean)
├── objects-set.mk             # Compiler flags (-fPIC)
├── README.md                  # This complete documentation guide
├── TESTING.md                 # Detailed testing dataset and benchmark results
├── test_prompts.json          # 6 curated job opportunity prompts with metadata
├── include/ttypt/
│   └── axil-resume-search.h   # C API header and state definitions
├── src/
│   └── libaxil-resume-search.c # Pure-C implementation (upload, pdftotext, sepal, UI)
├── test_resumes/              # Canonical full 1-page A4 test resumes (styled after ~/cv)
│   ├── paulo_lead_frontend_systems_architect.pdf
│   ├── elena_rust_distributed_storage_engineer.pdf
│   ├── marcus_ml_nlp_infra_engineer.pdf
│   ├── sophia_lead_product_designer.pdf
│   ├── david_devops_site_reliability_engineer.pdf
│   └── clara_embedded_firmware_iot_engineer.pdf
├── tests/
│   └── generate_test_resumes.py # Re-usable Python generator for test PDFs
└── uploads/                   # Runtime directory where uploaded PDFs are stored
```

---

## 🧪 Testing & Automated Evaluation

### Run Full Test Suite
Executes unit tests, error handling, upload formats, and the semantic evaluation harness:
```bash
make -C external/axil-resume-search test
```

### Run Evaluation Benchmark Only
Spawns an ephemeral Axil instance, uploads all 6 test resumes, queries all 6 job prompts, and verifies 100% top-rank accuracy:
```bash
./external/axil-resume-search/eval_matches.sh
```

---

## 🏛️ Architecture & C Internals

- **Event Loop & HTTP Dispatch**: Pure-C Axil daemon request dispatch via `axil_register_handler`. Zero external HTTP frameworks.
- **Under-the-Hood Extraction**: Executes `pdftotext -layout <file> -` via `popen()`, reading stdout into a dynamically growing memory buffer with pipe status validation.
- **Static File Streaming**: Uses `axil_respond_file(cfd, path, "pdf")` for streaming stored PDFs.
- **Dense Vector Embeddings**: Calls `axil-auren` on port 4242 using `sepal_embed_fetch()`, writing dense float vectors into `libsepal` vector store.
- **Lexical Indexing**: Uses `liblemma` to index full-text tokens with accent preservation (`pão ≠ pao`).
- **Metadata Management**: Uses `libcorm` string key-value maps linking `ref` ID to candidate JSON records and extracted text.
