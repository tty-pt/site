# Sibling Repositories & External Services

This document contains operational, build, and test guides for sibling repositories and services that integrate with or run alongside this site.

---

## axil-nd (sibling module port) — build, run & test

Sibling repo **`~/axil-nd`**: NeverDark MUCK engine as a generic axil module
(`libaxil-nd.so`), port status + remaining work in `~/nd/ND_PORT.md` (what is
missing to complete the port; the old full-plan file was removed). Builds
against the **system-installed**
axil/corm/xylem (`/usr/bin/axil`, headers `/usr/include/ttypt`, libs
`/lib`+`/usr/lib`).

```sh
make                  # builds lib/libaxil-nd.so (+ lib/axil-nd.so SONAME link)
./test                # RUN the suite (test.sh). NOTE: `make test` only bakes
                      #   test.sh into a `test` artifact (mk recipe) — it does
                      #   NOT run it. Use ./test or ./test.sh.
```

Run manually (module loader appends `.so`, so pass the SONAME path):

```sh
( LD_LIBRARY_PATH=$PWD/lib axil -d -A -p 28000 -m /home/quirinpa/axil-nd/lib/axil-nd & )
```

Known issues:
- `test.sh` flakes ~1 in 4 (`FAIL: no 101 in response` — real connection gets
  `HTTP/1.1 200 OK`+COOP instead of 101). Recorded in `~/nd/ND_PORT.md`
  (Known quirks);
  first check: does `~/axil-tty/test.sh` (identical structure) flake too?
- `make` prints `find: './htdocs': No such file or directory` — harmless
  (htdocs/ is gitignored/absent, same as axil-tty).
- After debug sessions run `pgrep -x axil` to catch stray axil daemons left
  on fixed ports.

Key axil wiring (verified from axil source): `on_axil_connect` fires ONLY on
WS upgrade; raw TCP accepts use the weak `axil_accept` hook; returning <0
from `on_axil_parse` skips `cmd_parse` (which routes HTTP methods, so it must
pass through for HTTP/WS-upgrade requests). See `~/nd/ND_PORT.md` (Library
usage findings + Locked decisions).

---

## axil-auren (local LLM inference engine) — build, run & test

Local LLM inference, OpenAI-compatible streaming chat, and dense vector embeddings engine
running on port 4242 (`libaxil-auren.so`). Full guide and Pi agent setup in `docs/LOCAL_LLM.md`.
To build custom semantic and hybrid search engines with `axil-auren`, `libsepal`, and `liblemma`, see `docs/SEMANTIC_SEARCH.md`.

```sh
# Turnkey server launcher:
/home/quirinpa/axil-auren/scripts/auren-server

# Run standalone engine unit suite:
LD_LIBRARY_PATH=/home/quirinpa/axil-auren/lib \
/home/quirinpa/axil-auren/bin/engine-test ~/.models/qwen2.5-0.5b-instruct.gguf
```

---

## axil-resume-search (PDF resume ingestion & job semantic search HTTP service) — build, run & test

Pure-C Axil service module providing `POST /resume` (PDF upload via multipart/raw/JSON, `pdftotext` extraction, file persistence in `uploads/`, vector generation via `axil-auren` on port 4242, persistent vector storage in `libsepal`, lexical indexing in `liblemma`, metadata in `libcorm`), `GET /candidates` (candidate semantic and hybrid matching against job opportunity descriptions), `GET /resume/file` (PDF file download), and an interactive browser UI form at `GET /`.

```sh
# Turnkey launcher (runs on :8092 by default):
./external/axil-resume-search/run.sh

# Run end-to-end integration test suite:
make -C external/axil-resume-search test
```

Full documentation and curl examples in `external/axil-resume-search/README.md`.
