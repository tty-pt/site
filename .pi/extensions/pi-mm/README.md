# pi-mm

Memory Mipmaps for the Pi coding agent: persistent, hierarchical, low-cost recall across compaction and sessions without holding raw transcripts in context and without requiring a model, a daemon, or a network connection.

`pi-mm` wraps the phase-2 `qmap` CLI composition surface (`external/libqmap` with `libjoint` + `libstoma`) into five agent tools. Storage is local: `.pi/mm/mem.db` (`QM_AINDEX:string`, ref = `uint32_t`) plus axis stores `joint.db`, `stoma.db`, and the roster sidecar `<primary>.roster`. The surface is byte-identical to `mm-plan/CLI-SURFACE-EXAMPLES.md` §8; the only new boundary is the Pi tool layer.

## How memory works

Every memory is one line `<DATE>:<TEXT>` (e.g. `2026-09-15:Beacon Harbor lights`). Joint indexes the leading date; stoma indexes the text. The agent picks the ref as `max(bare refs from qmap -g .)+1` per store. No single-flag reset exists — reset is the enumerate+forget loop (skips the `-1` sentinel; idempotent). See `mm-plan/README.md` §1 and `PHASE-2-CLI.md` + `CLI-SURFACE-EXAMPLES.md` §8 for the full dialect and conventions (U3 settled, F1–F6, D13, rank D2).

## Tools

| Tool | Params | Effect |
|------|--------|--------|
| `memory_store` | `text` (required), `timestamp` | Stores `<DATE>:<TEXT>` under a new ref. |
| `memory_scan` | `topic`, `level` (0 all-time/1 today/2 this month), `limit` | Searches topic at a time window (joint is bounded, pure text at level 0). |
| `memory_think` | `key` (ref or topic), `extract` (`date`/`text`/`whole`) | Recalls a payload (splits an ISO timestamp before the delimiter `:`). |
| `memory_forget` | `key` | Deletes on primary + every roster axis (idempotent). |
| `memory_reset` | — | Forgets every ref (enumerate bare + `grep -E '^[0-9]+$'` form). |

Degradation, never error: missing binary, empty inputs, zero matches, or nonzero exits return a diagnostic — never a thrown error. Text is accent-sensitive (`Pão≠pao`).

## Installation

Drop into `.pi/extensions/pi-mm/` in the `site` repo; `/reload` in Pi. No `npm install`. Requires the in-site `external/libqmap/bin/qmap` and sibling axis libs `external/lib{joint,stoma}/lib` (built via the site `Makefile`) unless overridden by settings.

## Configuration

Optional `.pi/settings.json` under `"pi-mm"`:

```json
{ "pi-mm": { "qmapBin": "/usr/bin/qmap", "axisLibs": "/a:/b", "memDir": ".pi/mm", "scanLimit": 10 } }
```

Precedence: settings → `QMAP_BIN`/`QMAP_AXIS_PATH` env → `PATH` `qmap` → in-site fallback. Defaults: `memDir=.pi/mm`, `scanLimit=10`. Unit tests stub qmap and never shell out; real-qmap coverage is `scripts/integration-mm.sh`.

## Development

```bash
deno test --allow-all --sloppy-imports --node-modules-dir=none tests/
deno run --allow-all scripts/check-complexity.ts
deno lint src/ tests/
npm --prefix .pi/extensions/pi-mm run zip   # → pi-mm-bundle.zip at project root
sh scripts/integration-mm.sh                # real qmap + joint+stoma, ephemeral dir
```

Budgets: file <350 LOC, function <80 LOC. Every commit must leave the extension loadable-and-graceful (no throws on missing state, no imports of missing modules).

## Layout

`src/config.ts` (defaults + resolution), `src/window.ts` (level→joint window, always bounded per F2), `src/resolve.ts` (bare-ref parse + sentinel skip + nextRef), `src/qmap.ts` (QmapRunner + invocation builders pinned to §8), `src/tools/*` (store/scan/think/forget/reset), `skills/pi-mm/SKILL.md`, `scripts/{zip_bundle,check-complexity,integration-mm}.`.
