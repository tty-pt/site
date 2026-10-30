# Testing & Evaluation Guide: axil-resume-search

This document details the test candidate resumes, job opportunity prompts, automated evaluation harness, and manual verification workflows for `axil-resume-search`.

---

## 1. Overview of the Testing Pipeline

The evaluation pipeline validates the complete end-to-end functionality:
1. **Multi-format Ingestion**: PDF files are uploaded via HTTP `POST /resume` (`multipart/form-data`, raw binary `application/pdf`, or base64 JSON).
2. **Text Extraction**: The system invokes `pdftotext -layout <file> -` under the hood via `popen()`, extracting the complete textual resume into memory.
3. **Physical File Storage**: The binary PDF file is saved to `uploads/resume_<ref>_<filename>.pdf`.
4. **Vector Embedding**: Dense 384-dimensional embeddings (`bge-small`) are generated via the `axil-auren` daemon on port `4242` (`/v1/embeddings`) and stored in `libsepal`.
5. **Lexical Indexing & Metadata**: Keyword tokens are indexed in `liblemma`, while candidate records, file paths, and snippet summaries are persisted in `libcorm`.
6. **Semantic Candidate Search**: Natural language job opportunity prompts are submitted via `GET /candidates?job=<prompt>&mode=hybrid`. The engine returns ranked candidate matches with similarity scores, stored file paths, and download links (`/resume/file?ref=<id>`).

---

## 2. Test Resume Dataset (`test_resumes/`)

All 6 resumes are full, dense **1-page A4** documents (595.28 x 841.89 pt) rendered in the exact design and color palette of `~/cv/style.mom` (Navy `#17263C`, Slate `#1E293B`, Teal `#0D9488`, Faint `#64748B`, Pill `#EBF4F3`):

| File | Candidate Name | Primary Domain & Full 1-Page Career Depth |
|---|---|---|
| `paulo_lead_frontend_systems_architect.pdf` | **Paulo Quirino** | **Lead Frontend & Systems Architect**: Full 8-role career from `~/cv/cv.mom` (Loomtech, Mov.ai, PrimeIT, UNPARALLEL, Mobinteg, Exictos, MIT-I, CEHA). C99/C11, WebAssembly (WASM), POSIX event loops, low-latency networking, React 18, TypeScript, Valtio, Zod, BabylonJS 3D robotics telemetry, and component reuse. |
| `elena_rust_distributed_storage_engineer.pdf` | **Elena Rostova** | **Senior Distributed Storage Engineer**: 8+ years across ByteScale Systems, CloudData Labs, Nordic Infrastructure, and Ericsson Research. Asynchronous Rust, Tokio, Raft consensus algorithm, LSM-tree storage internals (RocksDB), write-ahead logging (WAL), and Linux `io_uring`. |
| `marcus_ml_nlp_infra_engineer.pdf` | **Marcus Vance** | **Staff ML Infrastructure Engineer**: 9+ years across HyperScale AI, NeuralStack, VisionCore, and Stanford AI Lab. Large-scale GPU cluster serving (500+ H100s), PyTorch, CUDA kernel optimization, vLLM continuous batching & PagedAttention, TensorRT-LLM, Triton, and dense vector embeddings. |
| `sophia_lead_product_designer.pdf` | **Sophia Lindqvist** | **Lead Product & Design Systems Designer**: 8+ years across NordicUI, CreativeFlow SaaS, Studio Bold, and Danish Design Agency. Figma variables and design tokens, atomic design component libraries, visual typography hierarchies, user research, and WCAG 2.2 AAA accessibility. |
| `david_devops_site_reliability_engineer.pdf` | **David Chen** | **Principal Site Reliability Engineer (SRE)**: 10+ years across FinCloud Payments, DataMesh, Pacific Hosting, and Northwest Telco. Multi-region Kubernetes orchestration, Terraform IaC, eBPF networking with Cilium, Prometheus observability, Linux kernel sysctl tuning, and chaos engineering. |
| `clara_embedded_firmware_iot_engineer.pdf` | **Clara Mendes** | **Embedded Systems & IoT Firmware Engineer**: 6+ years across SensorLogic IoT, SmartGrid Devices, InnoWave, and INESC TEC. Bare-metal C, FreeRTOS task scheduling, ARM Cortex-M and ESP32 microcontrollers, serial communication (SPI, I2C, UART), and ultra-low-power BLE wireless sensor nodes. |

---

## 3. Job Opportunity Test Prompts (`test_prompts.json`)

The job prompts are crafted in natural language to evaluate cross-domain semantic discrimination:

### Job 1: Systems-Sympathetic Frontend Lead
> *"We are seeking a Lead Frontend Engineer and Systems Architect who bridges modern reactive web ergonomics (React 18, TypeScript, Valtio, Zod) with low-level mechanical sympathy in pure C, WebAssembly (WASM), and POSIX event loops. Experience in building real-time robotics telemetry tables, 3D interactive scene manipulation (BabylonJS), autonomous mobile robot interfaces, and datatype-driven component systems is required."*
- **Target Candidate**: **Paulo Quirino**

### Job 2: Distributed Storage Engine Architect
> *"High-throughput database infrastructure company is hiring a Senior Distributed Systems Engineer. You will design and implement a fault-tolerant key-value storage engine in asynchronous Rust with Tokio. Deep expertise in the Raft consensus algorithm, LSM-tree internals (SSTables, MemTables, Bloom filters), write-ahead logging (WAL), and zero-copy Linux asynchronous I/O using io_uring is required."*
- **Target Candidate**: **Elena Rostova**

### Job 3: LLM Inference & GPU Serving Infrastructure
> *"Leading AI research laboratory is looking for a Staff Machine Learning Infrastructure Engineer to scale our large language model inference clusters. Candidates must have extensive experience with PyTorch, CUDA kernel optimization, vLLM PagedAttention, TensorRT-LLM, Triton Inference Server, continuous batching, and high-throughput dense vector embedding search architectures."*
- **Target Candidate**: **Marcus Vance**

### Job 4: Design Systems & UX Design Lead
> *"We are looking for a Lead Product Designer to own and govern our company-wide design system across web and mobile applications. The role requires deep mastery of Figma variables and design tokens, atomic design component libraries, visual typography hierarchies, user journey research, and ensuring strict WCAG 2.2 AAA accessibility compliance."*
- **Target Candidate**: **Sophia Lindqvist**

### Job 5: Cloud Platform & SRE Principal
> *"Global fintech infrastructure provider seeks a Principal Site Reliability Engineer (SRE). Must have extensive hands-on experience operating mission-critical multi-region Kubernetes clusters, writing Terraform infrastructure as code, deploying eBPF networking with Cilium, managing Prometheus/Grafana observability, Linux kernel sysctl tuning, and automated chaos engineering."*
- **Target Candidate**: **David Chen**

### Job 6: Bare-Metal Firmware & IoT Engineer
> *"Industrial IoT manufacturer is hiring an Embedded Systems and Firmware Engineer for battery-powered sensor nodes. Requirements include bare-metal C programming on ARM Cortex-M and ESP32 microcontrollers, FreeRTOS task synchronization, low-level hardware peripheral buses (I2C, SPI, UART, CAN), Bluetooth Low Energy (BLE 5.2) stacks, and ultra-low-power sleep optimization."*
- **Target Candidate**: **Clara Mendes**

---

## 4. Automated Evaluation & Results

Run the full evaluation benchmark:
```bash
./external/axil-resume-search/eval_matches.sh
```
Or as part of the test target:
```bash
make -C external/axil-resume-search test
```

### Empirical Evaluation Matrix (Full 1-Page A4 Resumes)

| # | Job Opportunity Description | Target Candidate | Matched Candidate (#1) | Similarity Score | Status |
|---|---|---|---|---|---|
| 1 | Lead Systems Frontend Architect (React, WASM, Pure C) | **Paulo Quirino** | **Paulo Quirino** | **63.5%** | **PASS** |
| 2 | Senior Distributed Storage Systems Engineer (Rust, Raft, LSM) | **Elena Rostova** | **Elena Rostova** | **74.7%** | **PASS** |
| 3 | Staff ML Infrastructure Engineer (LLM Serving, PyTorch, CUDA) | **Marcus Vance** | **Marcus Vance** | **65.8%** | **PASS** |
| 4 | Lead Product & Design Systems Designer (Figma, WCAG) | **Sophia Lindqvist** | **Sophia Lindqvist** | **72.4%** | **PASS** |
| 5 | Principal Site Reliability Engineer (Kubernetes, eBPF, SRE) | **David Chen** | **David Chen** | **67.2%** | **PASS** |
| 6 | Senior Embedded Systems & IoT Firmware (Bare-Metal C, FreeRTOS) | **Clara Mendes** | **Clara Mendes** | **74.8%** | **PASS** |

**Result: 6/6 (100%) accuracy across all candidate specializations.**

---

## 5. Manual Reproduction with `curl`

### Step 1: Start the Service
```bash
./external/axil-resume-search/run.sh
```

### Step 2: Upload a Resume
```bash
curl -s -X POST http://localhost:8092/resume \
  -F "file=@external/axil-resume-search/test_resumes/paulo_lead_frontend_systems_architect.pdf;type=application/pdf"
```

### Step 3: Match Candidates with a Job Opportunity
```bash
curl -s "http://localhost:8092/candidates?job=Lead+Frontend+Engineer+with+WASM+and+C+experience&mode=hybrid"
```

### Step 4: Download the Stored PDF
```bash
curl -O -J "http://localhost:8092/resume/file?ref=1"
```

---

## 6. Interactive Browser Dashboard

1. Open `http://localhost:8092/` in any browser.
2. Select any PDF from `external/axil-resume-search/test_resumes/` in the upload card.
3. Paste a job opportunity prompt into the search card.
4. Click **Find Matching Candidates** to view ranked match cards with similarity scores, excerpts, and PDF download links.
