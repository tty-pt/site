# mm — Memory Mipmaps engine

**Hierarchical persistent memory for coding agents, with lexical and optional
semantic recall — offline-first, provider-free by default.**

`mm` is a small C CLI implementing the **Memory Mipmaps** model
(`~/llm/memory-mipmaps.md`): long-term memory organized as *mipmap levels*
(2 = summary → 1 = condensed → 0 = raw transcript), stored locally on
[libqmap](https://github.com/ttypt/libqmap) with an embedded full-text index
from this site's `external/libstoma`. It is the reference engine the
`pi-mem` extension will drive (`~/llm/MM.md`, Phase 2).

It exists to give an agent session persistent, hierarchical, low-cost recall
across compaction and sessions — without holding raw transcripts in context
and without requiring a model, a daemon, or a network connection.

## Why it exists

A long-running coding agent forgets. Context compacts, sessions end, and the
"gist" of earlier work — decisions, findings, what-happened-when-and-why —
evaporates. Naive fixes fail:

- keeping raw transcripts in context burns the token budget;
- git/docs/quest journals capture *state*, not *episodic history*;
- a "memory" that needs a local embedding model or a daemon stops working on
  modest machines and offline environments.

Memory Mipmaps turns recall into a **hierarchical lookup**: start from a
1-line summary (level 2), zoom to a condensed insight (level 1), drill to the
raw transcript (level 0). Each level is deliberately tiny, so the agent only
ever pulls what it needs.

`mm` is built around one assumption:

> **Memory is local data, and recall is a guided search, not an inference.**

## What mm provides

### Hierarchical mipmap memory

Entries are stored under level-derived keys:

| Level | Resolution | Key form | Example |
|-------|-----------|----------|---------|
| 2 | summary | `topic@month` | `mirror@2025-05` |
| 1 | condensed | `topic@timestamp` | `mirror@2025-05-14T1705` |
| 0 | raw | `@timestamp` | `@2025-05-14T1705` |

Level 0 carries no topic. Timestamps in keys have colons stripped by design
(libqmap record maps interpret `:` in composite keys; the canonical key form
avoids that entirely). The `ts` field always holds the true ISO timestamp.

### Guided recall (lexical, offline)

`mm scan` composes four independent routing axes, newest-first:

- `--topic T` — keys starting with `T@` (gist-first: pull level 2, zoom to 1, drill to 0);
- `--prefix P` — keys starting with `P` (zoom to an exact key);
- `--q` — full-text over the inverted index (see below);
- `--level N` — restrict to one mipmap level (`-1` = any).

Full-text semantics match site search: tokens are **prefix-AND**; a quoted
value (`--q '"phrase"'`) requires a **contiguous phrase**; matching is
accent-sensitive (`pão` ≠ `pao`).

Multi-axis composition is delegated to the `rec_query` engine in libqmap
(`rec_axis_register` + `rec_query_run`, per-axis AND/OR/NOT joins; see
`external/libqmap/docs/RECALL-KERNEL.md` and the plan at
`.opencode/plans/PLAN-REC-QUERY.md`). mm maps its scan flags onto
`rec_query_t` — `--when` → the time axis, `--where` → the space axis,
`--q` → the stoma (text) axis, `--vec`/`--like` → the libsepal (meaning)
axis. The engine is a consumer-side convenience; mm's offline paths work
with no provider and no plugin loaded.

### Optional semantic recall (offline primitives)

Vectors can be attached to entries (`mm vec put`) and scanned by cosine
similarity with **no provider and no network**:

- `mm scan --vec "0.9 0.1 0.0"` — top-k by cosine against a raw query vector
  (space-separated values; dimension = count);
- `mm scan --like mirror@2025-05` — "more like this" from a stored vector;

Entries without a stored vector, or with a different dimension than the query,
are skipped. Results include a trailing `\tscore` column and sort by score
descending. `--min-sim F` drops scores below a threshold; `--max` still caps.
Vectors hold up to **`MM_VEC_MAX` (2048) dimensions**; over-cap input is an
error, never a silent truncation.

Semantic search is a **routing aid, never the sole index** — time, topic, and
full-text always work, exactly the `MM.md` §2.2 degradation rule.

### Optional provider embeddings (config-only, never required)

`mm store --embed` and `mm scan --embed "text"` compute embeddings through any
OpenAI-compatible endpoint via system `curl`:

| Variable | Meaning |
|----------|---------|
| `MM_EMBED_URL` | endpoint (e.g. `https://host/v1/embeddings`) |
| `MM_EMBED_KEY` | API key (optional) |
| `MM_EMBED_MODEL` | model name (optional; provider default) |

No config, no curl, or a failed request degrades cleanly: the tool tells you
to use `--vec`/`--like` and everything else keeps working. Embeddings are a
convenience over the offline primitives — never a dependency.

### Forget / reset

`mm forget --key K` drops one entry (and its vector, and its index entries);
`mm reset` drops everything. Memory is explicit: nothing is auto-deleted.

## Protocol mapping

The original model's daemon dialect, mapped onto the CLI (`~/llm/MM.md` §4):

| Memory Mipmaps command | mm CLI |
|------------------------|--------|
| `memory:scan "topic" level 2` | `mm scan --topic T --level 2` |
| `memory:scan "topic@ts" level 1` | `mm scan --prefix T@ts --level 1` |
| `memory:scan "topic@ts" level 0` | `mm scan --prefix T@ts --level 0` |
| `memory:store timestamp [insight]` | `mm store --level N --topic T --ts TS --text '…'` |
| `memory:forget topic\|tag` | `mm forget --key K` |
| `memory:reset` | `mm reset` |
| `memory:think …` | (a provider-side extraction; lands in the `pi-mem` extension) |

`mm get` (read one entry), `--q` full-text, `--tags`, and the semantic flags
are additions beyond the published protocol.

## Quickstart

```sh
# build + test (top-level suite already includes mm)
make mm                       # or: make -C external/mm
make standalone-unit-tests    # or: make -C external/mm test

# store the levels, gist-first
mm store --level 2 --topic mirror --ts 2025-05 --text "May gist: mirror and rain."
mm store --level 1 --topic mirror --ts 2025-05-14T17:05 --text "Condensed: puddle reflection."
mm store --level 0 --ts 2025-05-14T17:05 --text "user: I saw my reflection in a puddle."

# recall — from gist down to raw
mm scan --topic mirror --level 2
mm scan --prefix mirror@2025-05-14T1705 --level 1
mm scan --prefix "@2025-05-14T1705"            # level 0
mm scan --topic mirror --q "puddle reflection"  # full-text

# semantic — offline primitives
mm vec put --key mirror@2025-05 --text "1 0 0"
mm scan --vec "0.9 0.1 0"
mm scan --like mirror@2025-05

# semantic — optional provider (config-only)
MM_EMBED_URL=http://host/v1/embeddings mm store --embed --level 2 --topic mirror \
  --ts 2025-05 --text "May gist: mirror and rain."

mm forget --key mirror@2025-05
mm reset
```

`mm` uses `$HOME/.mm/memory.qmap` by default; override with `--file PATH`.
The vector companion file is always `PATH + ".vec"`.

## Data & persistence

- Each map lives in its **own file**: entries in `<path>`, vectors in `<path>.vec`.
  (libqmap does not round-trip multiple maps sharing one file across processes —
  save/load order is positional — so one map per file is a hard invariant.)
- Data is checkpointed with `qmap_save()` on every mutation, and again by
  libqmap's exit destructor. Entries and vectors survive process exit and reopen.
- The full-text index (libstoma) is **not** persisted as its own store: it is
  rebuilt lazily from the qmap file on first search (mirrors hyle's
  `source.c:642-680` pattern), so the store is the single source of truth.

## Design principles

- **The engine never depends on a provider.** libqmap + libstoma + cosine are
  all in-process and local. Provider embedding is a config-gated CLI
  convenience, never a requirement of the engine.
- **Memory stays local.** Only vectors and synthesized text may ever travel to
  a provider; the qmap file never leaves the machine.
- **Semantic is a routing aid, never the sole index.** Time, topic, and
  full-text recall always work, model or no model.
- **Degradation is explicit.** No config / no curl / request failure → a
  precise `mm` message pointing at `--vec`/`--like`, and the rest of the tool
  is unaffected.

## Architecture

```
+----------------+     +---------------------+     +--------------------+
|  mm CLI (C)    | --> | libqmap (records,   | --> | <path>  (entries)  |
|  store/scan/   |     |  mipmap keys,       |     | <path>.vec (vecs)  |
|  get/forget/   |     |  vectors, sorted)   |     +--------------------+
|  reset/vec     |     +---------------------+
|   +-- stoma:   |     +---------------------+
|   |  FTS lazy- | --> | libstoma (inverted  |     in-memory index,
|   |  rebuilt)  |     |  index: prefix/AND, |     rebuilt from the
|   +------------+     |  phrase, accents)   |     qmap file on demand
+----------------+     +---------------------+     +--------------------+
        |                       |                 |  optional provider   |
        |  (optional, config)   v                 |  (OpenAI-compatible) |
        +-- --embed -- curl --> "$MM_EMBED_URL"  |  vectors only, never |
                                 +----------------+  the memory store    |
```

Multi-axis scan composition runs through **`rec_query`** in libqmap
(`rec_query_run` with AND/OR/NOT joins over dlopen'able axis plugins:
time / space / text / meaning). mm maps its scan flags onto `rec_query_t`;
its hand-rolled join/min-sim rank loop is retired. See
`external/libqmap/docs/RECALL-KERNEL.md` "Query engine & plugin registry".

## Package structure

```
external/mm/
  Makefile          # clang/cc toolchain; links qmap + stoma from repo roots
  src/engine.h      # public API: open/close/store/forget/reset/get/scan/vec
  src/engine.c      # engine: qmap records, key math, FTS, cosine top-k
  src/mm.c          # CLI: store/scan/get/forget/reset/vec (+ --embed)
  src/engine_test.c # unit tests (mkstemp-backed)
  test.sh           # unit + end-to-end CLI suite (includes stub-curl embed)
  README.md         # this file
```

## Testing

- `make -C external/mm test` — the full suite (unit + CLI);
- `make standalone-unit-tests` — runs it inside the site's quality gate.

The CLI suite covers routing, order, prefix zoom, FTS (AND, phrase, accents),
vectors + cosine, and the semantic path — including `--embed` driven by a
**stub `curl`** emitting a canned embedding response, proving the provider flag
end-to-end with zero real network.

## What this project is — and isn't

- **Is:** the local memory engine for Memory Mipmaps — deterministic, offline,
  provider-free, sub-1000-lines, testable. The **store** (store/get/forget)
  and the map of CLI flags onto `rec_query` composition.
- **Is not:** an embedding provider, a neural model, a chat daemon, a
  replacement for quest journals, or the multi-axis search compiler — that
  last job belongs to the `rec_query` engine in libqmap (axis registry +
  join semantics + plugin loading). mm **does not** link, curl, or otherwise
  depend on the `~/llm` repo. The `pi-mem` extension (`~/llm/MM.md`,
  Phase 2–4) wires these tools into the agent with gist-first recall rules.