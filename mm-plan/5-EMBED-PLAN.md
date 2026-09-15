# 5-EMBED-PLAN.md — Phase 5: real embeddings via axil-qllm

Status: **DONE 2026-09-16** — all slices landed (5-1..5-7). Tracked here;
status lives in `README.md` §0 row 6. Followed the Phase-3/4 TDD pattern:
unit tests stub qmap + curl (never shell out in `deno test`), real-qmap
coverage in `scripts/integration-mm.sh` (+ `RUNNING.md` manual gate).

## Goal

Give the pi-mm extension a **semantic** recall dimension on top of the
current joint+stoma time+text dialect: `memory_store` persists embeddings
(sepal axis), `memory_scan embed=true` queries them — fed by axil-qllm's
OpenAI-compatible `/v1/embeddings`, locally served, no model in the request
path of the site.

## Non-goals / decisions

- **Legacy `mm` binary is dead** — do not touch `external/mm`; nobody calls
  `mm --embed` anymore. The phase-2 qmap surface carries it.
- **No C changes.** Store-time embedding already works (sepal embeds via
  curl when `QMAP_SEPAL_EMBED_URL`+`MODEL` are set, stores VEC1 blob). The
  only new work is: (a) conditionally add `sepal` to the filespec,
  (b) a query-time embedding helper in TS, (c) `embed=true` on scan.
- **Query-time embedding lives in the extension, not sepal.** Sepal's decode
  reads a pre-computed binary float file (`file=`/`qdim=`). pi-mm embeds the
  topic externally (curl to qllm), writes the float32 LE dump to a temp
  file, and merges a `sepal="file=… qdim=…"` leaf.
- **Dim guard**: reject models > `SEPAL_VEC_MAX` (2048) with a soft
  degradation, not an error (cap is in `libsepal/include/ttypt/sepal.h:33`).
  768-dim nomic-embed-text is well under.

## How it works today (verified, do not re-open)

- Store: `rec_axis_store` (sepal) → non-float value + `url` configured →
  `sepal_embed_fetch(value)` → curl POST
  `{"model":…,"input":…}` → parse first `"embedding"` array →
  `sepal_put` → VEC1 blob (magic VEC1, sketch, norm, 256-dim prefix) in the
  same qmap file. Raw text is **not** stored on sepal.
  (`external/libsepal/src/libsepal.c:1561-1596`, `451-510`)
- Query: `sepal_axis_decode("file=… qdim=… m=… min_sim=…")` reads LE
  float32s; `sepal_search` = Hamming prefilter (m candidates) + cosine
  rerank on dim prefix. (`libsepal.c:1043-1119`, `879-944`)
- Env config: `QMAP_SEPAL_EMBED_URL`+`MODEL` required together, `KEY`
  optional; read in `rec_axis_env_config()` (`libsepal.c:1153-1165`).

## Extension slices

### 5-1 Filespec gains sepal when configured

- `src/config.ts`: read `QMAP_SEPAL_EMBED_URL` (+`MODEL`) via `applyEnv`;
  add `embedUrl?: string`, `embedModel?: string` to `MmConfig`; a
  `sepalConfigured(env)` helper = both set.
- `src/qmap.ts`: `filespecFor(memDir, embed?: boolean)` appends `,sepal`
  when embed is on. **Keep the plain `mem.db@joint,stoma:a:s` default** so
  no-qllm runs are byte-identical (back-compat with §8 recipes).
- Wiring: tools pass `env.cfg.embedUrl ? "sepal" : undefined`.

### 5-2 Embedding helper `src/embed.ts`

- `embedQuery(url: string, model: string, text: string, exec): Promise<{vecFile: string, qdim: number} | null>`
  - POST `{"model":…,"input":…}` to `url` via `pi.exec` on `curl` (like
    `mm` did — dedupe the fork-exec curl approach, but in TS):
    `curl -sS -X POST <url> -H 'Content-Type: application/json' -d <body>`
  - robust JSON float extract: find `"embedding"`, then the `[…]`, parse
    doubles to strings → write LE float32 with a tiny pure-TS encoder
    (no deps). Write to a tempfile (e.g. `os.tmpdir()/mm-embed-<n>.bin`)
    and return path + count.
  - failure (curl nonzero / no array / count>2048) → `null`, never throw.
- `cleanupEmbed(files: string[])` helper to unlink temp vectors after the
  scan (best-effort; degradation never errors).

### 5-3 `memory_scan` gains `embed` (modal)

- `src/tools/scan.ts`: new boolean `embed` param.
  - when `embed=true` and sepal configured → embed, build
    `sepal="file=$vecFile qdim=$N m=10 min_sim=0.2"` (D-constants in
    `qmap.ts`), merge with existing joint/stoma expr:
    `(joint=… AND stoma=… AND sepal=…)`.
  - when `embed=true` but sepal NOT configured → soft diagnostic
    (`details: {embed: "unconfigured"}`), fall back to the plain text scan.
  - embed helper returns null → same soft fallback, `details:{embed:"no-vector"}`.
- `src/qmap.ts`: `scanExpr(topic, level, now, until?, sepalLeaf?)`
  appends/ANDs the opt-in sepal leaf. Exports the consts `SEPAL_M=10`,
  `SEPAL_MIN_SIM=0.2`.

### 5-4 `memory_store` sepal-aware filespec

- `src/tools/store.ts` + `think.ts`(get) pass the sepal-capable filespec
  when configured (store embeds via the C path; get/forget/reset are
  filespec-agnostic but must use the **same** filespec so the roster
  matches). Ensure `nextRef`/list uses the same filespec everywhere.

### 5-5 Tests (fake runner, no shell-out)

- New `tests/embed.test.ts`:
  - `embedQuery` arg bytes (curl invocation) byte-pinned.
  - float32 LE encoder correctness (known vector → bytes).
  - qdim>2048 → null; no `embedding` key → null; nonzero curl → null.
- `tests/tools.test.ts`: `memory_scan` with `embed:true` →
  sepal leaf in expr + temp vec file arg; unconfigured → soft diagnostic;
  fallback on null vector.
- `tests/args.test.ts`: `filespecFor(…, embed)` → `,sepal`; default still
  `joint,stoma`; scan arg order byte-identical with/without sepal leaf.
- `tests/config.test.ts`: `applyEnv` picks `QMAP_SEPAL_EMBED_*`.

### 5-6 Integration (`scripts/integration-mm.sh`)

- Gated on `python3` + `libsepal.so` (skip section when absent; CI never
  needs a live qllm). One-shot local HTTP mock (`http.server`, a single
  `handle_request()` returning canned OpenAI JSON `[0.1,0.2,0.3]`) feeds
  the store side: `-p 4:"2026-09-16:embedded lighthouse beacon"` on a
  fresh `mem-embed.db@joint,stoma,sepal:a:s` exits 0. Query with a
  hand-crafted `file=`/`qdim=3` leaf (written with `struct.pack("<3f")`)
  matching the stored vector → ref 4 found; a `[1,0,0]` leaf (cosine
  0.267) against `min_sim=0.4` → ref 4 excluded.
- Implementation note: the original netcat stub was unreliable here
  (piped stdin never reaches the client of `nc -l`); python3 is the mock.
  Query vectors must be written with `struct.pack`, **not** POSIX-sh
  `printf '\xHH'` — dash passes `\xHH` through literally (48 garbage
  bytes instead of 12 float bytes; `fread` then parsed garbage floats and
  the gate scored 0.93 on a vector that is really 0.267). Files are
  size-checked (12 bytes) before the query.
- TDD: red first on the new `embed` param, then implement.

### 5-7 Docs + packaging

- `skills/pi-mm/SKILL.md`: `memory_scan(…, embed=true)` + config env vars;
  boundary note (sepal stores vectors, not text).
- `.pi/extensions/pi-mm/README.md` + `AGENTS.md`: `src/embed.ts` in layout,
  budgets (<350 LOC/file, <80 LOC/fn), gates unchanged.
- `mm-plan/README.md`: row 6 + §6 item 6 + resume → phase 5; this doc.
- `RUNNING.md`: keep as the operator guide (already at repo root).

## Gates

- `deno test` green (unit, no shell-out), `deno lint` clean,
  `check-complexity.ts` ok, `scripts/integration-mm.sh` all green,
  root `make` W06 PASS, no `/usr` installs.
- `git`: extension is loadable-and-graceful at every commit; commit only
  when asked.
- Manual gate: `memory_store("…")` on a `,sepal` roster + `memory_scan(…,
  embed=true)` with live qllm returns semantically-related refs
  (`RUNNING.md` steps; not part of CI).

## Settled (landed 2026-09-16)

- **Stoma+joint+sepal one-payload constraint — ACCEPTED as-is.** sepal
  embeds the whole `2026-09-16:embedded lighthouse beacon` string (date
  prefix included); the extension cannot strip it because the C fan-out
  passes the whole payload to `rec_axis_store`. Empirical quality on
  synthetic 3-dim vectors: exact-match query → cosine 1.0, ref found;
  orthogonal-ish `[1,0,0]` → 0.267, gated out at `min_sim=0.4`. Sepal
  math verified clean on a fresh db (`0.1,0.2,0.3` direct store:
  `[1,0,0]`→0.267261, self→1.000000). Real-qllm semantic quality (768-dim
  nomic) stays the manual gate (`RUNNING.md`); if it smells noisy, the
  fallback is a separate text-only sepal entry per memory (Phase 5b,
  two `-p` calls) — not needed today.
- **Store-side hard failure rule confirmed**: sepal in the roster +
  unconfigured embed URL ⇒ EINVAL ⇒ `-p` returns `EXIT_FAILURE`
  (`qmap.c` fan-out). Hence `filespecFor(_, embed)` adds `,sepal` only
  when `sepalConfigured(cfg)` — never otherwise. Same for `-d` paths.