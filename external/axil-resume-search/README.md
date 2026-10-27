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
