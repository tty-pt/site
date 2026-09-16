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
export QMAP_SEPAL_EMBED_URL=http://localhost:4242/v1/embeddings
export QMAP_SEPAL_EMBED_MODEL=nomic-embed-text
export QMAP_AXIS_PATH=external/libsepal/lib:external/libjoint/lib:external/libstoma/lib
```

`QMAP_SEPAL_EMBED_KEY` is optional (Bearer token; none for the local qllm).

## 3. Store with embeddings

With sepal in the roster, `memory_store` auto-embeds the text via the
qllm server (stores the VEC1 blob; the raw text is only in stoma):

```sh
qmap -p 1:"2026-09-14:Beacon Harbor lights" "mem.db@joint,stoma,sepal:a:s"
```

## 4. Query with embeddings

Structure lives in `-X`; the runtime query text and score floor ride D14
plugin flags (`--query`/`--min-sim`), broadcast to every bound axis that
declares them (sepal embeds the text server-side, stoma full-text tokenizes):

```sh
qmap -X '(stoma="field=text matched=1" AND sepal)' -g . \
  --query='harbor lights' --min-sim=0.2 "mem.db@joint,stoma,sepal:a:s" -t 10
```

The Phase 6/7 `memory_scan embed=true` mode emits exactly this shape — bare
`sepal` leaf + static `stoma="field=text matched=1"`, with the flags
appended (no client curl, no temp vector). Precedence is leaf spec > CLI
flag > env, so a leaf `query=`/`min_sim=` still wins when present (backward
compatible). The Phase 5 `file=` form (`sepal="file=/tmp/q.bin qdim=768 …"`,
LE-float32 vector file) still works for pre-computed vectors.
Track: `mm-plan/6-QUERY-TEXT-PLAN.md` (Phase 5 record stays in
`mm-plan/5-EMBED-PLAN.md`).

## Notes

- pi-mm's text recall (stoma FTS) needs **no** qllm — this only adds the
  semantic (sepal) dimension to `memory_scan`.
- The standalone `mm` binary (`external/mm/bin/mm`) is **legacy** — the
  phase-2 qmap surface + pi-mm tools replace it; do not rely on `mm --embed`.
- Sepal stores only the vector, not the text; a sepal-only scan returns
  refs, not payloads — join with `stoma=` for text, or `memory_think`.
- Each `*.db` owns its axis stores (`<primary>-<axis>`, e.g.
  `mem.db-joint`, `mem.db-sepal` beside `mem.db`); a directory may host
  many databases without cross-talk. Track: `mm-plan/7-AXIS-NAMESPACE-PLAN.md`.