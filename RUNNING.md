# RUNNING — qllm embeddings with pi-mm (real embeddings)

How to run a local embedding server (axil-qllm) and use it with the
pi-mm extension for real semantic search over `memory_scan`.

## Prerequisites

- `~/libqllm` source checkout — the axil module `libaxil-qllm.so` and the
  `axil` binary (already in `LD_LIBRARY_PATH` in `start.sh`).
- An embeddings-capable GGUF model on disk. Instruct models (e.g.
  `qwen2.5-*instruct`) do not produce useful embeddings — use a dedicated
  embeddings model (`nomic-embed-text-v1.5.Q5_K_M.gguf` recommended,
  768-dim, fits the `SEPAL_VEC_MAX` 2048 cap). ~250 MB.

## 1. Start the qllm server

```sh
cd ~/libqllm
QLLM_MODEL_PATH=/path/to/nomic-embed-text-v1.5.Q5_K_M.gguf \
  axil -A -d -p 4242 -m libaxil-qllm
```

`-m libaxil-qllm` resolves from `~/libqllm/lib` (or pass an absolute
path without `.so`, e.g. `-m /home/you/libqllm/lib/libaxil-qllm`).
`-A` auto-authenticates (needed for clean session handling).

Verify it is up:

```sh
curl -sS -X POST localhost:4242/v1/embeddings \
  -d '{"input":"hello","model":"nomic-embed-text"}' \
  | jq '.data[0].embedding | length'    # → 768
```

## 2. Point sepal at it

setenv-style, per shell (also valid in `start.sh` before the site server):

```sh
export CORM_SEPAL_EMBED_URL=http://localhost:4242/v1/embeddings
export CORM_SEPAL_EMBED_MODEL=nomic-embed-text
export CORM_AXIS_PATH=external/libsepal/lib:external/libjoint/lib:external/libstoma/lib
```

`CORM_SEPAL_EMBED_KEY` is optional (Bearer token; none for the local qllm).

## 3. Store with embeddings

With sepal in the roster, `memory_store` auto-embeds the text via the
qllm server (stores the VEC1 blob; the raw text is only in stoma):

```sh
corm -p 1:"2026-09-14:Beacon Harbor lights" "mem.db@joint,stoma,sepal:a:s"
```

## 4. Query with embeddings

Structure lives in `-X` (bare axis names + AND/OR/EXCEPT/NOT); ALL runtime
values ride D14 plugin flags, broadcast to every bound axis that declares
them (joint window `--since/--until`; stoma `--field=text --matched=1`;
`--query` → sepal embeds the text server-side + stoma full-text tokenizes;
`--min-sim` score floor):

```sh
corm -X '(joint AND stoma AND sepal)' -g . \
  --field=text --matched=1 --since=2026-09-14 --until=2026-09-16 \
  --query='harbor lights' --min-sim=0.2 "mem.db@joint,stoma,sepal:a:s" -t 10
```

The Phase 6/7 `memory_scan embed=true` mode emits exactly this shape (bare
names only; the plugin flags appended). Precedence is leaf spec > CLI
flag > env, so the old leaf grammar (`stoma="field=text query='…' matched=1"`
/ `joint="a=… b=…"`) still parses and wins per field (backward compatible).
The Phase 5 `file=` form (`sepal="file=/tmp/q.bin qdim=768 …"`, LE-float32
vector file) still works for pre-computed vectors.
Track: `mm-plan/6-QUERY-TEXT-PLAN.md` (Phase 5 record stays in
`mm-plan/5-EMBED-PLAN.md`).

## Notes

- pi-mm's text recall (stoma FTS) needs **no** qllm — this only adds the
  semantic (sepal) dimension to `memory_scan`.
- The standalone `mm` binary (`external/mm/bin/mm`) is **legacy** — the
  phase-2 corm surface + pi-mm tools replace it; do not rely on `mm --embed`.
- Sepal stores only the vector, not the text; a sepal-only scan returns
  refs, not payloads — join with `stoma=` for text, or `memory_think`.
- Each `*.db` owns its axis stores (`<primary>-<axis>`, e.g.
  `mem.db-joint`, `mem.db-sepal` beside `mem.db`); a directory may host
  many databases without cross-talk. Track: `mm-plan/7-AXIS-NAMESPACE-PLAN.md`.