# axil-resume-search

A pure-C [Axil](https://github.com/tty-pt/axil) service module for PDF resume ingestion and semantic candidate-to-job matching.

Upload PDF resumes, automatically extract clean text via `pdftotext` under the hood, persist the physical PDF file to disk, compute dense vector embeddings with `axil-auren` (port 4242), and semantically match and rank candidates against job opportunity descriptions with `libsepal` and `liblemma`.

---

## Architecture

- **HTTP & File Server**: Pure-C Axil daemon request dispatch via `axil_register_handler` and `axil_respond_file`.
- **Intake Pipeline**: Multi-format ingestion supporting `multipart/form-data` (browser file uploads), raw binary `application/pdf`, and base64 JSON payloads.
- **PDF Extraction**: Invokes `pdftotext -layout <file> -` under the hood via `popen()`, capturing full text into memory with error detection and validation.
- **Persistent File Storage**: Stores physical PDF files in `uploads/resume_<ref>_<filename>.pdf` with path sanitization and directory creation.
- **Vector Search Engine**: [libsepal](https://github.com/tty-pt/libsepal) two-stage ANN pipeline (Hamming sketch prefilter + exact cosine rerank).
- **Lexical Search Engine**: [liblemma](https://github.com/tty-pt/liblemma) inverted index with accent-preserving token folding (`pão ≠ pao`).
- **Metadata & Text Storage**: [libcorm](https://github.com/tty-pt/libcorm) persistent string maps storing candidate metadata (file path, file size, snippet, timestamp) and full extracted text.
- **Embeddings Backend**: Interoperates with `axil-auren` (`http://127.0.0.1:4242/v1/embeddings`) using OpenAI-compatible JSON payloads (`bge-small` 384-dimensional or `bge-base` 768-dimensional dense vectors).

---

## Prerequisites

1. Ensure the `axil-auren` embedding daemon is running on port `4242`:
   ```bash
   curl -s http://127.0.0.1:4242/v1/embeddings \
     -H "Content-Type: application/json" \
     -d '{"model":"bge-small","input":"ping"}'
   ```
   *(Expected response contains: `"embedding": [...]`)*

2. `pdftotext` CLI utility (part of `poppler-utils`):
   ```bash
   pdftotext -v
   ```

3. Runtime libraries: `libaxil.so`, `libxylem.so`, `libcorm.so`, `libsepal.so`, `liblemma.so`, `libjson-c.so`.

---

## Build & Run

### 1. Turnkey Launcher (Recommended)
Launch the service directly (auto-builds if needed and runs on port 8092 by default):
```bash
./external/axil-resume-search/run.sh
```
Or customize the port:
```bash
PORT=8095 ./external/axil-resume-search/run.sh
```

### 2. Manual Build & Run
Compile the shared library:
```bash
make -C external/axil-resume-search
```
This produces `lib/libaxil-resume-search.so`.

Run the Axil server with the module loaded:
```bash
axil -d -p 8092 -m /home/quirinpa/site/external/axil-resume-search/lib/libaxil-resume-search
```

### 3. Run the Test Suite & Semantic Evaluation
Execute the unit tests and the 6-profile semantic evaluation:
```bash
make -C external/axil-resume-search test
```
Or run the semantic matching evaluation directly:
```bash
./external/axil-resume-search/eval_matches.sh
```

For complete documentation on the test candidate dataset, job opportunity prompts, and benchmark results, see [`TESTING.md`](TESTING.md).

---

## REST Endpoints & `curl` Usage

### 1. Upload a PDF Resume (`POST /resume` or `POST /api/resume`)

#### A. Multipart Form Upload (Standard Web / Form):
```bash
curl -s -X POST http://localhost:8092/resume \
  -F "candidate=Alice Smith - Systems Engineer" \
  -F "file=@alice_resume.pdf;type=application/pdf"
```

#### B. Raw Binary Upload:
```bash
curl -s -X POST "http://localhost:8092/resume?candidate=Bob+Jones&filename=bob_resume.pdf" \
  -H "Content-Type: application/pdf" \
  --data-binary "@bob_resume.pdf"
```

#### C. Base64 JSON Upload:
```bash
B64=$(base64 -w 0 resume.pdf)
curl -s -X POST http://localhost:8092/resume \
  -H "Content-Type: application/json" \
  -d "{\"candidate\":\"Carol White\",\"filename\":\"carol.pdf\",\"pdf_base64\":\"$B64\"}"
```

#### Example Response:
```json
{
  "ok": true,
  "ref": 1,
  "candidate": "Alice Smith - Systems Engineer",
  "file_name": "alice_resume.pdf",
  "file_path": "uploads/resume_1_alice_resume.pdf",
  "file_size": 24510,
  "text_len": 3412,
  "dim": 384,
  "snippet": "Alice Smith - Senior Systems Engineer. 10 years experience developing high-performance Linux network daemons in pure C...",
  "file_url": "/resume/file?ref=1"
}
```

---

### 2. Match Candidates to Job Opportunities (`GET /candidates` or `GET /search`)

Search candidate resumes using a natural language job opportunity description.

```bash
curl -s "http://localhost:8092/candidates?job=Senior+C+systems+programmer+with+Linux+sockets+and+networking&mode=hybrid"
```

#### Query Parameters:
- `job` or `q`: The job description or role requirements.
- `mode`: Search mode:
  - `sepal` (default): Pure semantic vector similarity.
  - `hybrid`: Semantic vector similarity boosted by keyword overlap.
  - `lemma`: Lexical keyword matching.
- `k` or `limit`: Maximum candidates to return (default: 10).
- `min_sim`: Minimum similarity threshold (default: 0.0).

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
      "file_name": "alice_resume.pdf",
      "file_path": "uploads/resume_1_alice_resume.pdf",
      "file_size": 24510,
      "snippet": "Alice Smith - Senior Systems Engineer Summary: 10 years experience developing high-performance Linux network daemons in pure C...",
      "file_url": "/resume/file?ref=1",
      "lexical_match": true
    }
  ]
}
```

---

## Example Job Opportunities for Testing (Copy & Paste)

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

### 3. Download / View Original Stored PDF (`GET /resume/file?ref=<id>`)

Stream the stored PDF file directly to the browser or client with `Content-Type: application/pdf`:
```bash
curl -O -J "http://localhost:8092/resume/file?ref=1"
```

---

### 4. List All Ingested Candidates (`GET /candidates/list` or `GET /resumes`)

```bash
curl -s http://localhost:8092/candidates/list
```

---

### 5. Interactive Browser UI (`GET /` and `GET /ui`)

Open `http://localhost:8092/` in your browser for an interactive web dashboard:
- **Resume Upload Card**: File picker / drag-and-drop for PDF resumes and candidate names.
- **Job Opportunity Matcher**: Job description input with instant semantic and hybrid matching.
- **Ranked Candidate Cards**: Live similarity percentages, snippets, and direct PDF view/download links.
