---
name: pi-mm
description: "Memory Mipmaps (pi-mm): persistent hierarchical recall over local qmap files. Agent tools memory_scan/think/store/forget/reset."
---

# pi-mm — Memory Mipmaps

Persistent, hierarchical, low-cost recall over local `qmap` files. No model, no daemon, no network. Memory lives in `.pi/mm/mem.db` (primary `QM_AINDEX:string` + roster `joint,stoma`).

## Boundary rule (with pi-quest)

- **Quest** = current work state (findings, decisions, plan).
- **MM** = episodic history (what happened when and why).
- Never duplicate the same fact in both.

## Levels

| Level | Resolution | Key form | Window in `memory_scan` |
|-------|------------|----------|-------------------------|
| 2 | summary | `topic@month` | since start of this month |
| 1 | condensed | `topic@timestamp` | since start of today |
| 0 | raw | `@timestamp` | all-time (no time filter) |

Store is level-agnostic: every entry is `<DATE>:<TEXT>` (joint leading-date + stoma text). Scan's `level` shapes the joint window. Degradation never errors; missing `qmap` binary, empty matches, or nonzero exits return a diagnostic detail — not a thrown error. Text is accent-sensitive (`Pão≠pao`, no transliteration).

## Tools

- `memory_store(text, timestamp?)` — stores `<DATE>:<TEXT>` under an auto-picked numeric ref (`max+1` over bare `qmap -g .`). `timestamp` defaults to now (`YYYY-MM-DD`). Returns `{ref, key:"@DATE"}`.
- `memory_scan(topic, level?=0, limit?=10)` — searches by topic at a time window. Level 0 is pure text; 1/2 add a bounded `joint="a=… b=…"` (required by F2). Returns `{records:[{ref, score?, record}]}`.
- `memory_think(key, extract?="whole")` — recalls the payload for a numeric ref or a topic resolved by a top-1 scan. `extract` is `date|text|whole` (splits the ISO timestamp before the delimiter `:`). Returns `{ref, date?, text?, payload?}`.
- `memory_forget(key)` — deletes a memory by numeric ref or topic on the primary and every roster axis (idempotent, roster-backed). Returns `{ref, removed}`.
- `memory_reset()` — enumerates bare refs (`qmap -g .`) and forgets each (skips the `-1` sentinel). Returns `{refsForgotten}`. Idempotent; re-running is a no-op.

All tools shell out to the built `qmap` binary exactly per `mm-plan/CLI-SURFACE-EXAMPLES.md` §8; invocations carry `QMAP_AXIS_PATH` and run with `cwd` = the memory dir.

## Configuration

Optional `.pi/settings.json` under `"pi-mm"`:

```json
{ "pi-mm": { "qmapBin": "/usr/bin/qmap", "axisLibs": "/a:/b", "memDir": ".pi/mm", "scanLimit": 10 } }
```

Resolution: `settings.json` → `QMAP_BIN`/`QMAP_AXIS_PATH` env → `PATH` `qmap` → in-site `external/libqmap/bin/qmap`. Unit tests stub qmap and never shell out.

## Degradation

- Empty `text`/`topic`/`key`, no matches, or a failed `qmap` invocation → a `mm:*` message with `details.error`, never a thrown error.
- Store/forget/reset are idempotent; re-running does no harm.

## Examples

```
memory_store("Beacon Harbor lights", "2026-09-15")
memory_scan("beacon", 1)
memory_think("2", "text")
memory_forget("beacon")
memory_reset()
```
